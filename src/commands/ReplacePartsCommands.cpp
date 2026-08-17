#include "commands/ReplacePartsCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    dpp::cluster* g_bot =
        nullptr;

    Database* g_database =
        nullptr;


    struct ConfigRecord
    {
        bool found = false;

        long long id = 0;

        std::string publicId;
        std::string sourceUploadId;
        std::string scriptName;
        std::string game;
        std::string platform;
    };


    struct ReplaceSession
    {
        std::uint64_t userId = 0;

        std::string username;

        ConfigRecord config;

        int totalParts = 1;
        int nextPart = 1;

        std::vector<std::string> parts;
    };


    struct ReplaceResult
    {
        bool success = false;

        int oldPartCount = 0;
        int newPartCount = 0;

        std::string backupBatch;
        std::string error;
    };


    std::unordered_map<
        std::uint64_t,
        ReplaceSession
    > g_sessions;


    std::mutex g_sessionMutex;


    /*
     * ============================================================
     * BASIC HELPERS
     * ============================================================
     */

    std::string columnText(
        sqlite3_stmt* statement,
        int column
    )
    {
        const unsigned char* value =
            sqlite3_column_text(
                statement,
                column
            );


        if (value == nullptr)
        {
            return "";
        }


        return reinterpret_cast<
            const char*
        >(
            value
            );
    }


    dpp::message ephemeral(
        const std::string& content
    )
    {
        dpp::message message;

        message.set_content(
            content
        );

        message.set_flags(
            dpp::m_ephemeral
        );


        return message;
    }


    bool isOwner(
        std::uint64_t userId
    )
    {
        dpp::guild* guild =
            dpp::find_guild(
                MXChannels::MAIN_SERVER_ID
            );


        if (guild == nullptr)
        {
            return false;
        }


        return
            static_cast<std::uint64_t>(
                guild->owner_id
                ) ==
            userId;
    }


    std::string makeHash(
        const std::string& value
    )
    {
        const std::size_t hashValue =
            std::hash<std::string>{}(
                value
                );


        std::ostringstream stream;

        stream
            << std::hex
            << hashValue;


        return stream.str();
    }


    std::string makeBackupBatch()
    {
        const auto now =
            std::chrono::system_clock::now();


        const auto milliseconds =
            std::chrono::duration_cast<
            std::chrono::milliseconds
            >(
                now.time_since_epoch()
            ).count();


        return
            "REPLACE-" +
            std::to_string(
                milliseconds
            );
    }


    void rollback(
        sqlite3* database
    )
    {
        if (database == nullptr)
        {
            return;
        }


        sqlite3_exec(
            database,
            "ROLLBACK;",
            nullptr,
            nullptr,
            nullptr
        );
    }


    /*
     * ============================================================
     * TEMPORARY PUBLIC MESSAGE
     * ============================================================
     */

    void sendTemporaryMessage(
        const std::string& content,
        int seconds
    )
    {
        if (g_bot == nullptr)
        {
            return;
        }


        dpp::message message;

        message.set_channel_id(
            MXChannels::Main::UPLOAD_CONFIG
        );

        message.set_content(
            content
        );


        g_bot->message_create(
            message,

            [seconds](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error() ||
                    g_bot == nullptr
                    )
                {
                    return;
                }


                const dpp::message created =
                    callback.get<dpp::message>();


                const dpp::snowflake messageId =
                    created.id;


                const dpp::snowflake channelId =
                    created.channel_id;


                g_bot->start_timer(
                    [
                        messageId,
                        channelId
                    ](
                        const dpp::timer& timer
                        )
                    {
                        if (g_bot == nullptr)
                        {
                            return;
                        }


                        g_bot->message_delete(
                            messageId,
                            channelId
                        );


                        g_bot->stop_timer(
                            timer
                        );
                    },

                    seconds
                );
            }
        );
    }


    void sendTemporaryEmbed(
        const dpp::embed& embed,
        int seconds
    )
    {
        if (g_bot == nullptr)
        {
            return;
        }


        dpp::message message;

        message.set_channel_id(
            MXChannels::Main::UPLOAD_CONFIG
        );

        message.add_embed(
            embed
        );


        g_bot->message_create(
            message,

            [seconds](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error() ||
                    g_bot == nullptr
                    )
                {
                    return;
                }


                const dpp::message created =
                    callback.get<dpp::message>();


                const dpp::snowflake messageId =
                    created.id;


                const dpp::snowflake channelId =
                    created.channel_id;


                g_bot->start_timer(
                    [
                        messageId,
                        channelId
                    ](
                        const dpp::timer& timer
                        )
                    {
                        if (g_bot == nullptr)
                        {
                            return;
                        }


                        g_bot->message_delete(
                            messageId,
                            channelId
                        );


                        g_bot->stop_timer(
                            timer
                        );
                    },

                    seconds
                );
            }
        );
    }


    /*
     * ============================================================
     * HISTORY TABLE
     * ============================================================
     */

    bool ensureHistoryTable()
    {
        if (g_database == nullptr)
        {
            return false;
        }


        return g_database->execute(
            "CREATE TABLE IF NOT EXISTS config_part_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "script_id INTEGER NOT NULL, "
            "public_id TEXT NOT NULL, "
            "replacement_batch TEXT NOT NULL, "
            "part_number INTEGER NOT NULL, "
            "config_data TEXT NOT NULL, "
            "config_hash TEXT, "
            "replaced_by TEXT NOT NULL, "
            "replaced_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
        );
    }


    /*
     * ============================================================
     * FIND CONFIG
     * ============================================================
     */

    ConfigRecord findConfig(
        const std::string& publicId
    )
    {
        ConfigRecord result;


        if (g_database == nullptr)
        {
            return result;
        }


        sqlite3* database =
            g_database->handle();


        if (database == nullptr)
        {
            return result;
        }


        constexpr const char* sql =
            "SELECT "
            "id, "
            "public_id, "
            "COALESCE(source_upload_id, ''), "
            "script_name, "
            "game, "
            "platform "
            "FROM scripts "
            "WHERE LOWER(public_id) = LOWER(?) "
            "LIMIT 1;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return result;
        }


        sqlite3_bind_text(
            statement,
            1,
            publicId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        if (
            sqlite3_step(statement)
            == SQLITE_ROW
            )
        {
            result.found =
                true;


            result.id =
                sqlite3_column_int64(
                    statement,
                    0
                );


            result.publicId =
                columnText(
                    statement,
                    1
                );


            result.sourceUploadId =
                columnText(
                    statement,
                    2
                );


            result.scriptName =
                columnText(
                    statement,
                    3
                );


            result.game =
                columnText(
                    statement,
                    4
                );


            result.platform =
                columnText(
                    statement,
                    5
                );
        }


        sqlite3_finalize(
            statement
        );


        return result;
    }


    /*
     * ============================================================
     * COUNT CURRENT PARTS
     * ============================================================
     */

    int countCurrentParts(
        sqlite3* database,
        long long scriptId
    )
    {
        constexpr const char* sql =
            "SELECT COUNT(*) "
            "FROM script_parts "
            "WHERE script_id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return 0;
        }


        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        int count =
            0;


        if (
            sqlite3_step(statement)
            == SQLITE_ROW
            )
        {
            count =
                sqlite3_column_int(
                    statement,
                    0
                );
        }


        sqlite3_finalize(
            statement
        );


        return count;
    }


    /*
     * ============================================================
     * ARCHIVE OLD PARTS
     * ============================================================
     */

    bool archiveOldParts(
        sqlite3* database,
        const ReplaceSession& session,
        const std::string& batch
    )
    {
        constexpr const char* sql =
            "INSERT INTO config_part_history "
            "("
            "script_id, "
            "public_id, "
            "replacement_batch, "
            "part_number, "
            "config_data, "
            "config_hash, "
            "replaced_by"
            ") "
            "SELECT "
            "script_id, "
            "?, "
            "?, "
            "part_number, "
            "config_data, "
            "COALESCE(config_hash, ''), "
            "? "
            "FROM script_parts "
            "WHERE script_id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }


        sqlite3_bind_text(
            statement,
            1,
            session.config.publicId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            2,
            batch.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            3,
            session.username.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_int64(
            statement,
            4,
            session.config.id
        );


        const bool success =
            sqlite3_step(statement)
            == SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        return success;
    }


    /*
     * ============================================================
     * DELETE CURRENT SCRIPT PARTS
     * ============================================================
     */

    bool deleteScriptParts(
        sqlite3* database,
        long long scriptId
    )
    {
        constexpr const char* sql =
            "DELETE FROM script_parts "
            "WHERE script_id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }


        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        const bool success =
            sqlite3_step(statement)
            == SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        return success;
    }


    /*
     * ============================================================
     * INSERT NEW SCRIPT PARTS
     * ============================================================
     */

    bool insertScriptParts(
        sqlite3* database,
        const ReplaceSession& session
    )
    {
        constexpr const char* sql =
            "INSERT INTO script_parts "
            "("
            "script_id, "
            "part_number, "
            "config_data, "
            "config_hash"
            ") "
            "VALUES (?, ?, ?, ?);";


        for (
            std::size_t index = 0;
            index < session.parts.size();
            ++index
            )
        {
            sqlite3_stmt* statement =
                nullptr;


            if (
                sqlite3_prepare_v2(
                    database,
                    sql,
                    -1,
                    &statement,
                    nullptr
                ) != SQLITE_OK
                )
            {
                return false;
            }


            const std::string hash =
                makeHash(
                    session.parts[index]
                );


            sqlite3_bind_int64(
                statement,
                1,
                session.config.id
            );


            sqlite3_bind_int(
                statement,
                2,
                static_cast<int>(
                    index + 1
                    )
            );


            sqlite3_bind_text(
                statement,
                3,
                session.parts[index].c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                4,
                hash.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            const bool success =
                sqlite3_step(statement)
                == SQLITE_DONE;


            sqlite3_finalize(
                statement
            );


            if (!success)
            {
                return false;
            }
        }


        return true;
    }


    /*
     * ============================================================
     * SYNC ORIGINAL UPLOAD PARTS
     * ============================================================
     */

    bool syncUploadParts(
        sqlite3* database,
        const ReplaceSession& session
    )
    {
        if (
            session.config.sourceUploadId.empty()
            )
        {
            return true;
        }


        /*
         * Delete old upload parts.
         */

        constexpr const char* deleteSql =
            "DELETE FROM upload_parts "
            "WHERE upload_id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                deleteSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }


        sqlite3_bind_text(
            statement,
            1,
            session.config.sourceUploadId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        const bool deleteSuccess =
            sqlite3_step(statement)
            == SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        if (!deleteSuccess)
        {
            return false;
        }


        /*
         * Insert replacement upload parts.
         */

        constexpr const char* insertSql =
            "INSERT INTO upload_parts "
            "("
            "upload_id, "
            "part_number, "
            "config_data, "
            "config_hash"
            ") "
            "VALUES (?, ?, ?, ?);";


        for (
            std::size_t index = 0;
            index < session.parts.size();
            ++index
            )
        {
            statement =
                nullptr;


            if (
                sqlite3_prepare_v2(
                    database,
                    insertSql,
                    -1,
                    &statement,
                    nullptr
                ) != SQLITE_OK
                )
            {
                return false;
            }


            const std::string hash =
                makeHash(
                    session.parts[index]
                );


            sqlite3_bind_text(
                statement,
                1,
                session.config.sourceUploadId.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_int(
                statement,
                2,
                static_cast<int>(
                    index + 1
                    )
            );


            sqlite3_bind_text(
                statement,
                3,
                session.parts[index].c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                4,
                hash.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            const bool success =
                sqlite3_step(statement)
                == SQLITE_DONE;


            sqlite3_finalize(
                statement
            );


            if (!success)
            {
                return false;
            }
        }


        return true;
    }


    /*
     * ============================================================
     * TOUCH SCRIPT UPDATED DATE
     * ============================================================
     */

    bool updateScriptTimestamp(
        sqlite3* database,
        long long scriptId
    )
    {
        constexpr const char* sql =
            "UPDATE scripts "
            "SET updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }


        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        const bool success =
            sqlite3_step(statement)
            == SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        return success;
    }


    /*
     * ============================================================
     * REPLACE PARTS TRANSACTION
     * ============================================================
     */

    ReplaceResult replaceParts(
        const ReplaceSession& session
    )
    {
        ReplaceResult result;


        if (g_database == nullptr)
        {
            result.error =
                "Database service is unavailable.";


            return result;
        }


        sqlite3* database =
            g_database->handle();


        if (database == nullptr)
        {
            result.error =
                "Database connection is unavailable.";


            return result;
        }


        result.backupBatch =
            makeBackupBatch();


        result.oldPartCount =
            countCurrentParts(
                database,
                session.config.id
            );


        result.newPartCount =
            static_cast<int>(
                session.parts.size()
                );


        /*
         * Begin one transaction so a failed replacement
         * cannot leave half of the config replaced.
         */

        if (
            sqlite3_exec(
                database,
                "BEGIN IMMEDIATE TRANSACTION;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK
            )
        {
            result.error =
                sqlite3_errmsg(
                    database
                );


            return result;
        }


        /*
         * Archive old version first.
         */

        if (
            !archiveOldParts(
                database,
                session,
                result.backupBatch
            )
            )
        {
            result.error =
                "Could not archive the old config parts: " +
                std::string(
                    sqlite3_errmsg(
                        database
                    )
                );


            rollback(
                database
            );


            return result;
        }


        /*
         * Remove current public parts.
         */

        if (
            !deleteScriptParts(
                database,
                session.config.id
            )
            )
        {
            result.error =
                "Could not remove old script parts: " +
                std::string(
                    sqlite3_errmsg(
                        database
                    )
                );


            rollback(
                database
            );


            return result;
        }


        /*
         * Insert new public parts.
         */

        if (
            !insertScriptParts(
                database,
                session
            )
            )
        {
            result.error =
                "Could not save replacement parts: " +
                std::string(
                    sqlite3_errmsg(
                        database
                    )
                );


            rollback(
                database
            );


            return result;
        }


        /*
         * Keep upload_parts in sync.
         */

        if (
            !syncUploadParts(
                database,
                session
            )
            )
        {
            result.error =
                "Could not sync the original upload parts: " +
                std::string(
                    sqlite3_errmsg(
                        database
                    )
                );


            rollback(
                database
            );


            return result;
        }


        /*
         * Mark config as updated.
         */

        if (
            !updateScriptTimestamp(
                database,
                session.config.id
            )
            )
        {
            result.error =
                "Could not update the script timestamp: " +
                std::string(
                    sqlite3_errmsg(
                        database
                    )
                );


            rollback(
                database
            );


            return result;
        }


        /*
         * Commit everything.
         */

        if (
            sqlite3_exec(
                database,
                "COMMIT;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK
            )
        {
            result.error =
                sqlite3_errmsg(
                    database
                );


            rollback(
                database
            );


            return result;
        }


        result.success =
            true;


        return result;
    }


    /*
     * ============================================================
     * STORAGE LOGGING
     * ============================================================
     */

    void logReplacement(
        const ReplaceSession& session,
        const ReplaceResult& result
    )
    {
        if (g_bot == nullptr)
        {
            return;
        }


        const std::string details =
            "**Config:** `" +
            session.config.scriptName +
            "`\n"
            "**MX ID:** `" +
            session.config.publicId +
            "`\n"
            "**Game:** `" +
            session.config.game +
            "`\n"
            "**Platform:** `" +
            session.config.platform +
            "`\n"
            "**Old Parts:** `" +
            std::to_string(
                result.oldPartCount
            ) +
            "`\n"
            "**New Parts:** `" +
            std::to_string(
                result.newPartCount
            ) +
            "`\n"
            "**History Batch:** `" +
            result.backupBatch +
            "`\n"
            "**Replaced By:** `" +
            session.username +
            "`";


        StorageService::log(
            *g_bot,
            MXChannels::Storage::SYSTEM_LOGS,
            "🔄 Config Parts Replaced",
            details
        );


        StorageService::log(
            *g_bot,
            MXChannels::Storage::SCRIPT_METADATA,
            "🔄 Script Parts Updated",
            details
        );


        StorageService::log(
            *g_bot,
            MXChannels::Storage::SCRIPT_STORAGE,
            "📦 Config Version Updated",
            details
        );
    }
}


namespace ReplacePartsCommand
{
    /*
     * ============================================================
     * /replaceparts
     * ============================================================
     */

    void handle(
        const dpp::slashcommand_t& event
    )
    {
        const dpp::user& user =
            event.command.get_issuing_user();


        const std::uint64_t userId =
            static_cast<std::uint64_t>(
                user.id
                );


        /*
         * Owner only.
         */

        if (
            !isOwner(
                userId
            )
            )
        {
            event.reply(
                ephemeral(
                    "❌ `/replaceparts` is restricted "
                    "to the MX Central owner."
                )
            );


            return;
        }


        /*
         * Only use it in #upload-config so raw XIM
         * data never gets pasted around other channels.
         */

        if (
            event.command.channel_id !=
            MXChannels::Main::UPLOAD_CONFIG
            )
        {
            event.reply(
                ephemeral(
                    "📤 Use `/replaceparts` inside <#" +
                    std::to_string(
                        static_cast<std::uint64_t>(
                            MXChannels::Main::UPLOAD_CONFIG
                            )
                    ) +
                    ">."
                )
            );


            return;
        }


        std::string publicId;

        std::int64_t totalPartsValue =
            0;


        try
        {
            publicId =
                std::get<std::string>(
                    event.get_parameter(
                        "mx-id"
                    )
                );


            totalPartsValue =
                std::get<std::int64_t>(
                    event.get_parameter(
                        "parts"
                    )
                );
        }
        catch (...)
        {
            event.reply(
                ephemeral(
                    "❌ MX couldn't read the replacement information."
                )
            );


            return;
        }


        if (
            totalPartsValue < 1 ||
            totalPartsValue > 20
            )
        {
            event.reply(
                ephemeral(
                    "❌ Number of parts must be between "
                    "`1` and `20`."
                )
            );


            return;
        }


        const ConfigRecord config =
            findConfig(
                publicId
            );


        if (!config.found)
        {
            event.reply(
                ephemeral(
                    "❌ No config was found with MX ID `" +
                    publicId +
                    "`."
                )
            );


            return;
        }


        /*
         * Prevent a second replacement session.
         */

        {
            std::lock_guard<std::mutex>
                lock(
                    g_sessionMutex
                );


            if (
                g_sessions.find(
                    userId
                ) !=
                g_sessions.end()
                )
            {
                event.reply(
                    ephemeral(
                        "⚠️ You already have an active "
                        "config-part replacement session."
                    )
                );


                return;
            }


            ReplaceSession session;

            session.userId =
                userId;


            session.username =
                user.username;


            session.config =
                config;


            session.totalParts =
                static_cast<int>(
                    totalPartsValue
                    );


            session.nextPart =
                1;


            session.parts.reserve(
                static_cast<std::size_t>(
                    session.totalParts
                    )
            );


            g_sessions[
                userId
            ] = session;
        }


        /*
         * Starting confirmation.
         */

        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🔄 Replace Config Parts"
            )

            .set_description(
                "MX is ready to replace the stored parts for "
                "**" +
                config.scriptName +
                "**.\n\n"
                "Paste **Part 1** into this channel now."
            )

            .add_field(
                "MX ID",
                "`" +
                config.publicId +
                "`",
                true
            )

            .add_field(
                "New Part Count",
                "`" +
                std::to_string(
                    totalPartsValue
                ) +
                "`",
                true
            )

            .add_field(
                "Safety",
                "🟢 Old parts will be archived before replacement.",
                false
            )

            .add_field(
                "Important",
                "Finish any other upload/direct-add session "
                "before pasting replacement parts.",
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Config Part Replacement"
                )
            );


        dpp::message response;

        response.add_embed(
            embed
        );

        response.set_flags(
            dpp::m_ephemeral
        );


        event.reply(
            response
        );
    }


    /*
     * ============================================================
     * REGISTER HANDLERS
     * ============================================================
     */

    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    )
    {
        g_bot =
            &bot;


        g_database =
            &database;


        if (!ensureHistoryTable())
        {
            std::cerr
                << "[ReplaceParts] Could not create "
                << "config_part_history table."
                << std::endl;
        }


        /*
         * ========================================================
         * CAPTURE PASTED PARTS
         * ========================================================
         */

        bot.on_message_create(
            [&bot](
                const dpp::message_create_t& event
                )
            {
                /*
                 * Ignore bot messages.
                 */

                if (
                    event.msg.author.is_bot()
                    )
                {
                    return;
                }


                /*
                 * Main MX server only.
                 */

                if (
                    event.msg.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    return;
                }


                /*
                 * #upload-config only.
                 */

                if (
                    event.msg.channel_id !=
                    MXChannels::Main::UPLOAD_CONFIG
                    )
                {
                    return;
                }


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        event.msg.author.id
                        );


                ReplaceSession completedSession;

                bool sessionFound =
                    false;


                bool completed =
                    false;


                int savedPart =
                    0;


                int nextPart =
                    0;


                /*
                 * ====================================================
                 * CAPTURE EXACT RAW MESSAGE
                 * ====================================================
                 */

                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_sessionMutex
                        );


                    const auto found =
                        g_sessions.find(
                            userId
                        );


                    if (
                        found ==
                        g_sessions.end()
                        )
                    {
                        return;
                    }


                    sessionFound =
                        true;


                    ReplaceSession& session =
                        found->second;


                    if (
                        event.msg.content.empty()
                        )
                    {
                        return;
                    }


                    savedPart =
                        session.nextPart;


                    /*
                     * Store exact config data.
                     *
                     * No trimming.
                     * No code blocks.
                     * No alteration.
                     */

                    session.parts.push_back(
                        event.msg.content
                    );


                    session.nextPart++;


                    nextPart =
                        session.nextPart;


                    if (
                        static_cast<int>(
                            session.parts.size()
                            ) >=
                        session.totalParts
                        )
                    {
                        completedSession =
                            session;


                        completed =
                            true;


                        g_sessions.erase(
                            found
                        );
                    }
                }


                if (!sessionFound)
                {
                    return;
                }


                /*
                 * Delete the owner's raw pasted config.
                 */

                bot.message_delete(
                    event.msg.id,
                    event.msg.channel_id
                );


                /*
                 * ====================================================
                 * WAITING FOR MORE PARTS
                 * ====================================================
                 */

                if (!completed)
                {
                    sendTemporaryMessage(
                        "✅ **Part " +
                        std::to_string(
                            savedPart
                        ) +
                        " saved.** Paste **Part " +
                        std::to_string(
                            nextPart
                        ) +
                        "** now.",
                        5
                    );


                    return;
                }


                /*
                 * ====================================================
                 * REPLACE DATABASE PARTS
                 * ====================================================
                 */

                const ReplaceResult result =
                    replaceParts(
                        completedSession
                    );


                if (!result.success)
                {
                    std::cerr
                        << "[ReplaceParts] Replacement failed: "
                        << result.error
                        << std::endl;


                    sendTemporaryMessage(
                        "❌ **Config replacement failed.**\n"
                        "`" +
                        result.error +
                        "`\n\n"
                        "The previous public config was preserved "
                        "by the database transaction.",
                        15
                    );


                    return;
                }


                /*
                 * Storage server audit.
                 */

                logReplacement(
                    completedSession,
                    result
                );


                /*
                 * Success panel.
                 */

                dpp::embed embed;

                embed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "✅ Config Parts Replaced"
                    )

                    .set_description(
                        "**" +
                        completedSession.config.scriptName +
                        "** has been updated successfully."
                    )

                    .add_field(
                        "MX ID",
                        "`" +
                        completedSession.config.publicId +
                        "`",
                        true
                    )

                    .add_field(
                        "Old Parts",
                        "`" +
                        std::to_string(
                            result.oldPartCount
                        ) +
                        "`",
                        true
                    )

                    .add_field(
                        "New Parts",
                        "`" +
                        std::to_string(
                            result.newPartCount
                        ) +
                        "`",
                        true
                    )

                    .add_field(
                        "MX ID Preserved",
                        "🟢 `Yes`",
                        true
                    )

                    .add_field(
                        "Downloads Preserved",
                        "🟢 `Yes`",
                        true
                    )

                    .add_field(
                        "Ratings Preserved",
                        "🟢 `Yes`",
                        true
                    )

                    .add_field(
                        "Old Version",
                        "🟢 Archived as `" +
                        result.backupBatch +
                        "`",
                        false
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Config Parts Updated"
                        )
                    );


                sendTemporaryEmbed(
                    embed,
                    15
                );


                std::cout
                    << "[ReplaceParts] Updated "
                    << completedSession.config.publicId
                    << " with "
                    << result.newPartCount
                    << " part(s)."
                    << std::endl;
            }
        );
    }
}