#include "commands/AddScriptCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"
#include "services/LatestFeedService.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iomanip>
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

    const std::string MODAL_ID =
        "mx_addscript_modal";


    struct DirectAddSession
    {
        std::uint64_t userId = 0;
        std::uint64_t channelId = 0;

        std::string username;

        std::string scriptName;
        std::string game;
        std::string platform;
        std::string sensitivity;

        int totalParts = 1;
        int nextPart = 1;

        std::vector<std::string> parts;
    };


    struct DirectAddResult
    {
        bool success = false;

        std::string publicId;
        std::string uploadId;

        std::string error;
    };


    dpp::cluster* g_bot =
        nullptr;

    Database* g_database =
        nullptr;


    std::unordered_map<
        std::uint64_t,
        DirectAddSession
    > g_sessions;


    std::mutex g_sessionMutex;

    std::atomic<std::uint64_t>
        g_directCounter{ 0 };


    /*
     * ============================================================
     * HELPERS
     * ============================================================
     */

    std::string trim(
        const std::string& input
    )
    {
        const std::size_t first =
            input.find_first_not_of(
                " \t\r\n"
            );


        if (first == std::string::npos)
        {
            return "";
        }


        const std::size_t last =
            input.find_last_not_of(
                " \t\r\n"
            );


        return input.substr(
            first,
            last - first + 1
        );
    }


    std::string lower(
        std::string value
    )
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),

            [](
                unsigned char character
                )
            {
                return static_cast<char>(
                    std::tolower(character)
                    );
            }
        );


        return value;
    }


    std::string normalizePlatform(
        const std::string& value
    )
    {
        const std::string normalized =
            lower(
                trim(value)
            );


        if (
            normalized == "ps" ||
            normalized == "ps5" ||
            normalized == "ps4" ||
            normalized == "playstation" ||
            normalized == "playstation 5" ||
            normalized == "playstation5"
            )
        {
            return "PS";
        }


        if (
            normalized == "xbox" ||
            normalized == "xb" ||
            normalized == "xbox one" ||
            normalized == "xbox series x" ||
            normalized == "xbox series s"
            )
        {
            return "Xbox";
        }


        if (
            normalized == "pc" ||
            normalized == "windows" ||
            normalized == "computer"
            )
        {
            return "PC";
        }


        if (
            normalized == "not sure" ||
            normalized == "unsure" ||
            normalized == "idk" ||
            normalized == "unknown" ||
            normalized == "n/a" ||
            normalized == "na"
            )
        {
            return "Not Sure";
        }


        return "";
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


    std::string getModalValue(
        const dpp::form_submit_t& event,
        const std::string& customId
    )
    {
        for (
            const auto& component :
            event.components
            )
        {
            if (
                component.custom_id !=
                customId
                )
            {
                continue;
            }


            const auto* value =
                std::get_if<std::string>(
                    &component.value
                );


            if (value != nullptr)
            {
                return *value;
            }
        }


        return "";
    }


    std::string makeUploadId()
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
            "DIRECT-" +
            std::to_string(milliseconds) +
            "-" +
            std::to_string(
                ++g_directCounter
            );
    }


    std::string makeHash(
        const std::string& value
    )
    {
        const std::size_t hash =
            std::hash<std::string>{}(
                value
                );


        std::ostringstream stream;

        stream
            << std::hex
            << hash;


        return stream.str();
    }


    bool execSql(
        sqlite3* database,
        const char* sql
    )
    {
        char* errorMessage =
            nullptr;


        const int result =
            sqlite3_exec(
                database,
                sql,
                nullptr,
                nullptr,
                &errorMessage
            );


        if (result != SQLITE_OK)
        {
            if (errorMessage != nullptr)
            {
                std::cerr
                    << "[AddScript] SQLite: "
                    << errorMessage
                    << std::endl;


                sqlite3_free(
                    errorMessage
                );
            }


            return false;
        }


        return true;
    }


    void rollback(
        sqlite3* database
    )
    {
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
     * TEMPORARY CHANNEL MESSAGE
     * ============================================================
     */

    void temporaryMessage(
        const std::string& content,
        int seconds = 6
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


                const dpp::message sent =
                    callback.get<dpp::message>();


                const dpp::snowflake messageId =
                    sent.id;

                const dpp::snowflake channelId =
                    sent.channel_id;


                g_bot->start_timer(
                    [
                        messageId,
                        channelId
                    ](
                        const dpp::timer& timer
                        )
                    {
                        if (g_bot != nullptr)
                        {
                            g_bot->message_delete(
                                messageId,
                                channelId
                            );

                            g_bot->stop_timer(
                                timer
                            );
                        }
                    },

                    seconds
                );
            }
        );
    }


    /*
     * ============================================================
     * ENSURE OWNER USER
     * ============================================================
     */

    bool ensureCreator(
        sqlite3* database,
        const DirectAddSession& session
    )
    {
        constexpr const char* sql =
            "INSERT INTO users "
            "("
            "discord_id, "
            "username, "
            "is_creator"
            ") "
            "VALUES (?, ?, 1) "
            "ON CONFLICT(discord_id) DO UPDATE SET "
            "username = excluded.username, "
            "is_creator = 1, "
            "last_seen_at = CURRENT_TIMESTAMP;";


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


        const std::string userId =
            std::to_string(
                session.userId
            );


        sqlite3_bind_text(
            statement,
            1,
            userId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            2,
            session.username.c_str(),
            -1,
            SQLITE_TRANSIENT
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
     * FINALIZE DIRECT CONFIG
     * ============================================================
     */

    DirectAddResult finalizeDirectAdd(
        const DirectAddSession& session
    )
    {
        DirectAddResult result;


        if (g_database == nullptr)
        {
            result.error =
                "Database is unavailable.";

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


        result.uploadId =
            makeUploadId();


        if (
            !execSql(
                database,
                "BEGIN IMMEDIATE TRANSACTION;"
            )
            )
        {
            result.error =
                "Could not start database transaction.";

            return result;
        }


        /*
         * ========================================================
         * USER
         * ========================================================
         */

        if (
            !ensureCreator(
                database,
                session
            )
            )
        {
            rollback(database);

            result.error =
                "Could not save creator information.";

            return result;
        }


        const std::string userId =
            std::to_string(
                session.userId
            );


        /*
         * ========================================================
         * APPROVED UPLOAD RECORD
         * ========================================================
         */

        constexpr const char* uploadSql =
            "INSERT INTO uploads "
            "("
            "upload_id, "
            "uploader_id, "
            "script_name, "
            "game, "
            "platform, "
            "sensitivity, "
            "description, "
            "version, "
            "status, "
            "reviewer_id, "
            "reviewed_at"
            ") "
            "VALUES "
            "(?, ?, ?, ?, ?, ?, ?, ?, 'approved', ?, CURRENT_TIMESTAMP);";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                uploadSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            rollback(database);

            result.error =
                sqlite3_errmsg(database);

            return result;
        }


        const std::string description =
            "Added directly by MX Central owner";

        const std::string version =
            "1.0";


        sqlite3_bind_text(
            statement,
            1,
            result.uploadId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            2,
            userId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            3,
            session.scriptName.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            4,
            session.game.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            5,
            session.platform.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            6,
            session.sensitivity.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            7,
            description.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            8,
            version.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            9,
            userId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        if (
            sqlite3_step(statement)
            != SQLITE_DONE
            )
        {
            result.error =
                sqlite3_errmsg(database);

            sqlite3_finalize(
                statement
            );

            rollback(database);

            return result;
        }


        sqlite3_finalize(
            statement
        );


        /*
         * ========================================================
         * UPLOAD PARTS
         * ========================================================
         */

        constexpr const char* uploadPartSql =
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
                    uploadPartSql,
                    -1,
                    &statement,
                    nullptr
                ) != SQLITE_OK
                )
            {
                rollback(database);

                result.error =
                    sqlite3_errmsg(database);

                return result;
            }


            const std::string hash =
                makeHash(
                    session.parts[index]
                );


            sqlite3_bind_text(
                statement,
                1,
                result.uploadId.c_str(),
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


            if (
                sqlite3_step(statement)
                != SQLITE_DONE
                )
            {
                result.error =
                    sqlite3_errmsg(database);

                sqlite3_finalize(
                    statement
                );

                rollback(database);

                return result;
            }


            sqlite3_finalize(
                statement
            );
        }


        /*
         * ========================================================
         * PUBLIC SCRIPT
         * ========================================================
         */

        const std::string temporaryPublicId =
            "PENDING-" +
            result.uploadId;


        constexpr const char* scriptSql =
            "INSERT INTO scripts "
            "("
            "public_id, "
            "source_upload_id, "
            "creator_id, "
            "script_name, "
            "game, "
            "platform, "
            "sensitivity, "
            "description, "
            "version, "
            "verified, "
            "visibility, "
            "download_count, "
            "favourite_count"
            ") "
            "VALUES "
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, 1, 'public', 0, 0);";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                scriptSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            rollback(database);

            result.error =
                sqlite3_errmsg(database);

            return result;
        }


        sqlite3_bind_text(
            statement,
            1,
            temporaryPublicId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            2,
            result.uploadId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            3,
            userId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            4,
            session.scriptName.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            5,
            session.game.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            6,
            session.platform.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            7,
            session.sensitivity.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            8,
            description.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        sqlite3_bind_text(
            statement,
            9,
            version.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        if (
            sqlite3_step(statement)
            != SQLITE_DONE
            )
        {
            result.error =
                sqlite3_errmsg(database);

            sqlite3_finalize(
                statement
            );

            rollback(database);

            return result;
        }


        sqlite3_finalize(
            statement
        );


        const sqlite3_int64 scriptId =
            sqlite3_last_insert_rowid(
                database
            );


        /*
         * Generate MX-000001 style ID.
         */

        std::ostringstream publicIdStream;

        publicIdStream
            << "MX-"
            << std::setw(6)
            << std::setfill('0')
            << scriptId;


        result.publicId =
            publicIdStream.str();


        constexpr const char* publicIdSql =
            "UPDATE scripts "
            "SET public_id = ?, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ?;";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                publicIdSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            rollback(database);

            result.error =
                sqlite3_errmsg(database);

            return result;
        }


        sqlite3_bind_text(
            statement,
            1,
            result.publicId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_int64(
            statement,
            2,
            scriptId
        );


        if (
            sqlite3_step(statement)
            != SQLITE_DONE
            )
        {
            result.error =
                sqlite3_errmsg(database);

            sqlite3_finalize(
                statement
            );

            rollback(database);

            return result;
        }


        sqlite3_finalize(
            statement
        );


        /*
         * ========================================================
         * SCRIPT PARTS
         * ========================================================
         */

        constexpr const char* scriptPartSql =
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
            statement =
                nullptr;


            if (
                sqlite3_prepare_v2(
                    database,
                    scriptPartSql,
                    -1,
                    &statement,
                    nullptr
                ) != SQLITE_OK
                )
            {
                rollback(database);

                result.error =
                    sqlite3_errmsg(database);

                return result;
            }


            const std::string hash =
                makeHash(
                    session.parts[index]
                );


            sqlite3_bind_int64(
                statement,
                1,
                scriptId
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


            if (
                sqlite3_step(statement)
                != SQLITE_DONE
                )
            {
                result.error =
                    sqlite3_errmsg(database);

                sqlite3_finalize(
                    statement
                );

                rollback(database);

                return result;
            }


            sqlite3_finalize(
                statement
            );
        }


        /*
         * ========================================================
         * STATISTICS
         * ========================================================
         */

        execSql(
            database,
            "UPDATE statistics "
            "SET value = value + 1, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE key = 'total_scripts';"
        );


        execSql(
            database,
            "UPDATE statistics "
            "SET value = value + 1, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE key = 'total_uploads';"
        );


        execSql(
            database,
            "INSERT INTO daily_statistics "
            "(date, uploads, approved_uploads) "
            "VALUES (date('now'), 1, 1) "
            "ON CONFLICT(date) DO UPDATE SET "
            "uploads = uploads + 1, "
            "approved_uploads = approved_uploads + 1;"
        );


        /*
         * ========================================================
         * COMMIT
         * ========================================================
         */

        if (
            !execSql(
                database,
                "COMMIT;"
            )
            )
        {
            rollback(database);

            result.error =
                "Could not commit the config.";

            return result;
        }


        result.success =
            true;


        return result;
    }



    /*
     * ============================================================
     * STORAGE LOGS
     * ============================================================
     */

    void logDirectAdd(
        const DirectAddSession& session,
        const DirectAddResult& result
    )
    {
        if (g_bot == nullptr)
        {
            return;
        }


        const std::string details =
            "**Config:** `" +
            session.scriptName +
            "`\n"
            "**Game:** `" +
            session.game +
            "`\n"
            "**Platform:** `" +
            session.platform +
            "`\n"
            "**Sensitivity:** `" +
            session.sensitivity +
            "`\n"
            "**Parts:** `" +
            std::to_string(
                session.totalParts
            ) +
            "`\n"
            "**MX ID:** `" +
            result.publicId +
            "`\n"
            "**Upload ID:** `" +
            result.uploadId +
            "`\n"
            "**Added By:** `" +
            session.username +
            "`";


        StorageService::log(
            *g_bot,
            MXChannels::Storage::APPROVED_UPLOADS,
            "✅ Direct Config Added",
            details
        );


        StorageService::log(
            *g_bot,
            MXChannels::Storage::SCRIPT_METADATA,
            "📦 Script Metadata",
            details
        );


        StorageService::logUpload(
            *g_bot,
            session.username,
            session.scriptName,
            "Direct Owner Add"
        );
    }
}


namespace AddScriptCommand
{
    /*
     * ============================================================
     * /addscript
     * ============================================================
     */

    void open(
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
         * Server owner only.
         */

        if (
            !isOwner(
                userId
            )
            )
        {
            event.reply(
                ephemeral(
                    "❌ `/addscript` is restricted to the "
                    "MX Central server owner."
                )
            );

            return;
        }


        /*
         * Keep direct config entry inside #upload-config.
         */

        if (
            event.command.channel_id !=
            MXChannels::Main::UPLOAD_CONFIG
            )
        {
            event.reply(
                ephemeral(
                    "📤 Use `/addscript` inside <#" +
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


        /*
         * Don't accidentally start two sessions.
         */

        {
            std::lock_guard<std::mutex>
                lock(
                    g_sessionMutex
                );


            if (
                g_sessions.find(userId) !=
                g_sessions.end()
                )
            {
                event.reply(
                    ephemeral(
                        "⚠️ You already have an active "
                        "direct-add session.\n\n"
                        "Finish its remaining config parts first."
                    )
                );

                return;
            }
        }


        /*
         * Metadata modal.
         */

        dpp::interaction_modal_response modal(
            MODAL_ID,
            "Add MX Config"
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Config Name"
            )
            .set_id(
                "config_name"
            )
            .set_type(
                dpp::cot_text
            )
            .set_placeholder(
                "e.g. Rust Zero Recoil V3"
            )
            .set_min_length(1)
            .set_max_length(100)
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Game"
            )
            .set_id(
                "game"
            )
            .set_type(
                dpp::cot_text
            )
            .set_placeholder(
                "e.g. Rust"
            )
            .set_min_length(1)
            .set_max_length(100)
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Platform"
            )
            .set_id(
                "platform"
            )
            .set_type(
                dpp::cot_text
            )
            .set_placeholder(
                "PS / Xbox / PC / Not Sure"
            )
            .set_min_length(1)
            .set_max_length(50)
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "In-Game Sensitivity"
            )
            .set_id(
                "sensitivity"
            )
            .set_type(
                dpp::cot_text
            )
            .set_placeholder(
                "e.g. 100 / None / Unknown"
            )
            .set_min_length(1)
            .set_max_length(100)
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Number of Config Parts"
            )
            .set_id(
                "parts"
            )
            .set_type(
                dpp::cot_text
            )
            .set_placeholder(
                "1 - 20"
            )
            .set_min_length(1)
            .set_max_length(2)
        );


        event.dialog(
            modal
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


        LatestFeedService::initialize(
            database,
            bot
        );


        /*
         * ========================================================
         * MODAL SUBMISSION
         * ========================================================
         */

        bot.on_form_submit(
            [](
                const dpp::form_submit_t& event
                )
            {
                if (
                    event.custom_id !=
                    MODAL_ID
                    )
                {
                    return;
                }


                const dpp::user& user =
                    event.command.get_issuing_user();


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        user.id
                        );


                if (
                    !isOwner(
                        userId
                    )
                    )
                {
                    event.reply(
                        ephemeral(
                            "❌ Owner permission required."
                        )
                    );

                    return;
                }


                const std::string scriptName =
                    trim(
                        getModalValue(
                            event,
                            "config_name"
                        )
                    );


                const std::string game =
                    trim(
                        getModalValue(
                            event,
                            "game"
                        )
                    );


                const std::string platform =
                    normalizePlatform(
                        getModalValue(
                            event,
                            "platform"
                        )
                    );


                std::string sensitivity =
                    trim(
                        getModalValue(
                            event,
                            "sensitivity"
                        )
                    );


                const std::string partsText =
                    trim(
                        getModalValue(
                            event,
                            "parts"
                        )
                    );


                if (
                    scriptName.empty() ||
                    game.empty()
                    )
                {
                    event.reply(
                        ephemeral(
                            "❌ Config name and game are required."
                        )
                    );

                    return;
                }


                if (platform.empty())
                {
                    event.reply(
                        ephemeral(
                            "❌ Platform must be one of:\n"
                            "`PS` • `Xbox` • `PC` • `Not Sure`"
                        )
                    );

                    return;
                }


                if (sensitivity.empty())
                {
                    sensitivity =
                        "None";
                }


                int totalParts =
                    0;


                try
                {
                    totalParts =
                        std::stoi(
                            partsText
                        );
                }
                catch (...)
                {
                    totalParts =
                        0;
                }


                if (
                    totalParts < 1 ||
                    totalParts > 20
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


                DirectAddSession session;

                session.userId =
                    userId;

                session.channelId =
                    static_cast<std::uint64_t>(
                        event.command.channel_id
                        );

                session.username =
                    user.username;

                session.scriptName =
                    scriptName;

                session.game =
                    game;

                session.platform =
                    platform;

                session.sensitivity =
                    sensitivity;

                session.totalParts =
                    totalParts;

                session.nextPart =
                    1;

                session.parts.reserve(
                    static_cast<std::size_t>(
                        totalParts
                        )
                );


                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_sessionMutex
                        );


                    g_sessions[userId] =
                        session;
                }


                dpp::embed embed;

                embed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "⚡ Direct Add Started"
                    )

                    .set_description(
                        "**" +
                        scriptName +
                        "** will be added directly to MX Central.\n\n"
                        "Paste **Part 1** into this channel now."
                    )

                    .add_field(
                        "Game",
                        "`" +
                        game +
                        "`",
                        true
                    )

                    .add_field(
                        "Platform",
                        "`" +
                        platform +
                        "`",
                        true
                    )

                    .add_field(
                        "Parts",
                        "`" +
                        std::to_string(
                            totalParts
                        ) +
                        "`",
                        true
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Owner Direct Add"
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
        );


        /*
         * ========================================================
         * CAPTURE RAW CONFIG PARTS
         * ========================================================
         */

        bot.on_message_create(
            [&bot](
                const dpp::message_create_t& event
                )
            {
                /*
                 * Ignore bots.
                 */

                if (
                    event.msg.author.is_bot()
                    )
                {
                    return;
                }


                if (
                    event.msg.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    return;
                }


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


                DirectAddSession completedSession;

                bool foundSession =
                    false;

                bool completed =
                    false;

                int savedPart =
                    0;

                int nextPart =
                    0;

                int totalParts =
                    0;


                /*
                 * Store the raw text EXACTLY as sent.
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


                    foundSession =
                        true;


                    DirectAddSession& session =
                        found->second;


                    if (
                        event.msg.content.empty()
                        )
                    {
                        return;
                    }


                    savedPart =
                        session.nextPart;


                    session.parts.push_back(
                        event.msg.content
                    );


                    session.nextPart++;


                    totalParts =
                        session.totalParts;

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


                if (!foundSession)
                {
                    return;
                }


                /*
                 * Delete owner's raw config paste.
                 */

                bot.message_delete(
                    event.msg.id,
                    event.msg.channel_id
                );


                /*
                 * More parts remaining.
                 */

                if (!completed)
                {
                    temporaryMessage(
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
                 * FINAL SAVE
                 * ====================================================
                 */

                const DirectAddResult result =
                    finalizeDirectAdd(
                        completedSession
                    );


                if (!result.success)
                {
                    std::cerr
                        << "[AddScript] Direct add failed: "
                        << result.error
                        << std::endl;


                    temporaryMessage(
                        "❌ **Direct add failed.**\n"
                        "Database error: `" +
                        result.error +
                        "`",
                        12
                    );


                    /*
                     * Restore session so data isn't immediately lost.
                     */

                    {
                        std::lock_guard<std::mutex>
                            lock(
                                g_sessionMutex
                            );


                        completedSession.nextPart =
                            completedSession.totalParts + 1;


                        g_sessions[userId] =
                            completedSession;
                    }


                    return;
                }


                /*
                 * Storage logs.
                 */

                logDirectAdd(
                    completedSession,
                    result
                );


                /*
                 * Refresh the same persistent latest-releases
                 * message instead of creating another public post.
                 */

                LatestFeedService::refresh();


                /*
                 * Success confirmation.
                 */

                dpp::embed embed;

                embed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "✅ Config Added to MX"
                    )

                    .set_description(
                        "**" +
                        completedSession.scriptName +
                        "** is now live in the MX Central library."
                    )

                    .add_field(
                        "MX ID",
                        "`" +
                        result.publicId +
                        "`",
                        true
                    )

                    .add_field(
                        "Game",
                        "`" +
                        completedSession.game +
                        "`",
                        true
                    )

                    .add_field(
                        "Platform",
                        "`" +
                        completedSession.platform +
                        "`",
                        true
                    )

                    .add_field(
                        "Parts",
                        "`" +
                        std::to_string(
                            completedSession.totalParts
                        ) +
                        "`",
                        true
                    )

                    .add_field(
                        "Status",
                        "🟢 `Live`",
                        true
                    )

                    .add_field(
                        "Approval",
                        "`Bypassed • Owner Add`",
                        true
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Direct Add Complete"
                        )
                    );


                dpp::message successMessage;

                successMessage.set_channel_id(
                    MXChannels::Main::UPLOAD_CONFIG
                );

                successMessage.add_embed(
                    embed
                );


                bot.message_create(
                    successMessage
                );
            }
        );
    }
}