#include "commands/AdminCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    const std::string REMOVE_CONFIRM_ID =
        "mx_remove_confirm";

    const std::string REMOVE_CANCEL_ID =
        "mx_remove_cancel";

    const std::string EDIT_MODAL_PREFIX =
        "mx_editconfig:";


    dpp::cluster* g_bot =
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
        std::string sensitivity;

        std::string visibility;
    };


    struct PendingRemoval
    {
        std::string publicId;
        std::string reason;
    };


    std::unordered_map<
        std::uint64_t,
        PendingRemoval
    > g_pendingRemovals;


    std::mutex g_pendingMutex;


    /*
     * ============================================================
     * BASIC HELPERS
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
        std::string input
    )
    {
        std::transform(
            input.begin(),
            input.end(),
            input.begin(),

            [](
                unsigned char character
                )
            {
                return static_cast<char>(
                    std::tolower(character)
                    );
            }
        );


        return input;
    }


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


        return reinterpret_cast<const char*>(
            value
            );
    }


    bool startsWith(
        const std::string& value,
        const std::string& prefix
    )
    {
        return
            value.rfind(
                prefix,
                0
            ) == 0;
    }


    std::string normalizePlatform(
        const std::string& input
    )
    {
        const std::string value =
            lower(
                trim(input)
            );


        if (value.empty())
        {
            return "";
        }


        if (
            value == "ps" ||
            value == "ps4" ||
            value == "ps5" ||
            value == "playstation" ||
            value == "playstation 4" ||
            value == "playstation 5"
            )
        {
            return "PS";
        }


        if (
            value == "xbox" ||
            value == "xb" ||
            value == "xbox one" ||
            value == "xbox series x" ||
            value == "xbox series s"
            )
        {
            return "Xbox";
        }


        if (
            value == "pc" ||
            value == "windows" ||
            value == "computer"
            )
        {
            return "PC";
        }


        if (
            value == "not sure" ||
            value == "unsure" ||
            value == "unknown" ||
            value == "idk" ||
            value == "n/a" ||
            value == "na"
            )
        {
            return "Not Sure";
        }


        return "__INVALID__";
    }


    std::string makePlaceholder(
        const std::string& currentValue
    )
    {
        std::string result =
            "Current: " +
            currentValue;


        if (result.size() > 100)
        {
            result.resize(
                100
            );
        }


        return result;
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


    dpp::message ephemeralText(
        const std::string& text
    )
    {
        dpp::message message;

        message.set_content(
            text
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
                return trim(
                    *value
                );
            }
        }


        return "";
    }


    /*
     * ============================================================
     * MODERATION HISTORY
     * ============================================================
     */

    bool ensureModerationTable(
        Database& database
    )
    {
        return database.execute(
            "CREATE TABLE IF NOT EXISTS config_moderation_history ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "script_id INTEGER, "
            "public_id TEXT NOT NULL, "
            "action TEXT NOT NULL, "
            "reason TEXT, "
            "actor_id TEXT NOT NULL, "
            "actor_name TEXT NOT NULL, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
        );
    }


    void addModerationHistory(
        Database& database,
        const ConfigRecord& config,
        const std::string& action,
        const std::string& reason,
        std::uint64_t actorId,
        const std::string& actorName
    )
    {
        sqlite3* db =
            database.handle();


        if (db == nullptr)
        {
            return;
        }


        constexpr const char* sql =
            "INSERT INTO config_moderation_history "
            "("
            "script_id, "
            "public_id, "
            "action, "
            "reason, "
            "actor_id, "
            "actor_name"
            ") "
            "VALUES (?, ?, ?, ?, ?, ?);";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                db,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return;
        }


        const std::string actorIdText =
            std::to_string(
                actorId
            );


        sqlite3_bind_int64(
            statement,
            1,
            config.id
        );


        sqlite3_bind_text(
            statement,
            2,
            config.publicId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            3,
            action.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            4,
            reason.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            5,
            actorIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            6,
            actorName.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_step(
            statement
        );


        sqlite3_finalize(
            statement
        );
    }


    /*
     * ============================================================
     * FIND CONFIG
     * ============================================================
     */

    ConfigRecord findConfig(
        Database& database,
        const std::string& publicId
    )
    {
        ConfigRecord result;


        sqlite3* db =
            database.handle();


        if (db == nullptr)
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
            "platform, "
            "COALESCE(sensitivity, 'None'), "
            "visibility "
            "FROM scripts "
            "WHERE LOWER(public_id) = LOWER(?) "
            "LIMIT 1;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                db,
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


            result.sensitivity =
                columnText(
                    statement,
                    6
                );


            result.visibility =
                columnText(
                    statement,
                    7
                );
        }


        sqlite3_finalize(
            statement
        );


        return result;
    }


    /*
     * ============================================================
     * VISIBILITY
     * ============================================================
     */

    bool setVisibility(
        Database& database,
        long long scriptId,
        const std::string& visibility
    )
    {
        sqlite3* db =
            database.handle();


        if (db == nullptr)
        {
            return false;
        }


        constexpr const char* sql =
            "UPDATE scripts "
            "SET visibility = ?, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                db,
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
            visibility.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_int64(
            statement,
            2,
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
     * REMOVE PANEL
     * ============================================================
     */

    dpp::message buildRemovalConfirmation(
        const ConfigRecord& config,
        const std::string& reason
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⚠️ Remove MX Config?"
            )

            .set_description(
                "This hides the config from MX Central.\n\n"
                "**The config and all of its parts remain stored** "
                "and can be restored later."
            )

            .add_field(
                "Config",
                "`" +
                config.scriptName +
                "`",
                true
            )

            .add_field(
                "MX ID",
                "`" +
                config.publicId +
                "`",
                true
            )

            .add_field(
                "Game",
                "`" +
                config.game +
                "`",
                true
            )

            .add_field(
                "Platform",
                "`" +
                config.platform +
                "`",
                true
            )

            .add_field(
                "Status",
                "🟢 `Public`",
                true
            )

            .add_field(
                "Reason",
                reason,
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Owner Config Management"
                )
            );


        dpp::component removeButton;

        removeButton
            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_danger
            )

            .set_label(
                "Remove Config"
            )

            .set_id(
                REMOVE_CONFIRM_ID
            );


        dpp::component cancelButton;

        cancelButton
            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_secondary
            )

            .set_label(
                "Cancel"
            )

            .set_id(
                REMOVE_CANCEL_ID
            );


        dpp::component row;

        row.add_component(
            removeButton
        );

        row.add_component(
            cancelButton
        );


        dpp::message response;

        response.add_embed(
            embed
        );

        response.add_component(
            row
        );

        response.set_flags(
            dpp::m_ephemeral
        );


        return response;
    }


    dpp::message buildRemovedMessage(
        const ConfigRecord& config,
        const std::string& reason
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🗑️ Config Removed"
            )

            .set_description(
                "**" +
                config.scriptName +
                "** has been hidden from the public MX library."
            )

            .add_field(
                "MX ID",
                "`" +
                config.publicId +
                "`",
                true
            )

            .add_field(
                "Status",
                "🔴 `Hidden`",
                true
            )

            .add_field(
                "Data",
                "🟢 `Preserved`",
                true
            )

            .add_field(
                "Reason",
                reason,
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "Use /restoreconfig to make it public again"
                )
            );


        dpp::message response;

        response.add_embed(
            embed
        );


        return response;
    }


    /*
     * ============================================================
     * UPDATE METADATA
     * ============================================================
     */

    bool updateConfigMetadata(
        Database& database,
        const ConfigRecord& original,
        const std::string& scriptName,
        const std::string& game,
        const std::string& platform,
        const std::string& sensitivity
    )
    {
        sqlite3* db =
            database.handle();


        if (db == nullptr)
        {
            return false;
        }


        if (
            sqlite3_exec(
                db,
                "BEGIN IMMEDIATE TRANSACTION;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }


        constexpr const char* scriptSql =
            "UPDATE scripts "
            "SET script_name = ?, "
            "game = ?, "
            "platform = ?, "
            "sensitivity = ?, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                db,
                scriptSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            sqlite3_exec(
                db,
                "ROLLBACK;",
                nullptr,
                nullptr,
                nullptr
            );

            return false;
        }


        sqlite3_bind_text(
            statement,
            1,
            scriptName.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            2,
            game.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            3,
            platform.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            4,
            sensitivity.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_int64(
            statement,
            5,
            original.id
        );


        const bool scriptSuccess =
            sqlite3_step(
                statement
            ) == SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        if (!scriptSuccess)
        {
            sqlite3_exec(
                db,
                "ROLLBACK;",
                nullptr,
                nullptr,
                nullptr
            );

            return false;
        }


        /*
         * Keep original upload metadata matching the
         * public script record.
         */

        if (
            !original.sourceUploadId.empty()
            )
        {
            constexpr const char* uploadSql =
                "UPDATE uploads "
                "SET script_name = ?, "
                "game = ?, "
                "platform = ?, "
                "sensitivity = ? "
                "WHERE upload_id = ?;";


            statement =
                nullptr;


            if (
                sqlite3_prepare_v2(
                    db,
                    uploadSql,
                    -1,
                    &statement,
                    nullptr
                ) != SQLITE_OK
                )
            {
                sqlite3_exec(
                    db,
                    "ROLLBACK;",
                    nullptr,
                    nullptr,
                    nullptr
                );

                return false;
            }


            sqlite3_bind_text(
                statement,
                1,
                scriptName.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                2,
                game.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                3,
                platform.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                4,
                sensitivity.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            sqlite3_bind_text(
                statement,
                5,
                original.sourceUploadId.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            const bool uploadSuccess =
                sqlite3_step(
                    statement
                ) == SQLITE_DONE;


            sqlite3_finalize(
                statement
            );


            if (!uploadSuccess)
            {
                sqlite3_exec(
                    db,
                    "ROLLBACK;",
                    nullptr,
                    nullptr,
                    nullptr
                );

                return false;
            }
        }


        if (
            sqlite3_exec(
                db,
                "COMMIT;",
                nullptr,
                nullptr,
                nullptr
            ) != SQLITE_OK
            )
        {
            sqlite3_exec(
                db,
                "ROLLBACK;",
                nullptr,
                nullptr,
                nullptr
            );

            return false;
        }


        return true;
    }


    /*
     * ============================================================
     * EDIT SUMMARY
     * ============================================================
     */

    std::string buildEditSummary(
        const ConfigRecord& oldConfig,
        const std::string& newName,
        const std::string& newGame,
        const std::string& newPlatform,
        const std::string& newSensitivity
    )
    {
        std::string summary;


        if (
            oldConfig.scriptName !=
            newName
            )
        {
            summary +=
                "**Name:** `" +
                oldConfig.scriptName +
                "` → `" +
                newName +
                "`\n";
        }


        if (
            oldConfig.game !=
            newGame
            )
        {
            summary +=
                "**Game:** `" +
                oldConfig.game +
                "` → `" +
                newGame +
                "`\n";
        }


        if (
            oldConfig.platform !=
            newPlatform
            )
        {
            summary +=
                "**Platform:** `" +
                oldConfig.platform +
                "` → `" +
                newPlatform +
                "`\n";
        }


        if (
            oldConfig.sensitivity !=
            newSensitivity
            )
        {
            summary +=
                "**Sensitivity:** `" +
                oldConfig.sensitivity +
                "` → `" +
                newSensitivity +
                "`\n";
        }


        if (summary.empty())
        {
            return "No fields changed.";
        }


        return summary;
    }
}


namespace AdminCommand
{
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


        if (
            !ensureModerationTable(
                database
            )
            )
        {
            std::cerr
                << "[AdminCommand] Failed to create "
                << "moderation history table."
                << std::endl;
        }


        /*
         * ========================================================
         * REMOVE BUTTONS
         * ========================================================
         */

        bot.on_button_click(
            [&bot, &database](
                const dpp::button_click_t& event
                )
            {
                if (
                    event.custom_id !=
                    REMOVE_CONFIRM_ID &&
                    event.custom_id !=
                    REMOVE_CANCEL_ID
                    )
                {
                    return;
                }


                if (
                    event.command.guild_id !=
                    MXChannels::MAIN_SERVER_ID
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
                        ephemeralText(
                            "❌ Only the MX Central owner "
                            "can use this action."
                        )
                    );

                    return;
                }


                /*
                 * CANCEL
                 */

                if (
                    event.custom_id ==
                    REMOVE_CANCEL_ID
                    )
                {
                    {
                        std::lock_guard<std::mutex>
                            lock(
                                g_pendingMutex
                            );


                        g_pendingRemovals.erase(
                            userId
                        );
                    }


                    dpp::embed embed;

                    embed
                        .set_color(
                            MX_PURPLE
                        )

                        .set_title(
                            "✅ Removal Cancelled"
                        )

                        .set_description(
                            "No changes were made to the config."
                        );


                    dpp::message response;

                    response.add_embed(
                        embed
                    );


                    event.reply(
                        dpp::ir_update_message,
                        response
                    );


                    return;
                }


                /*
                 * CONFIRM
                 */

                PendingRemoval pending;

                bool foundPending =
                    false;


                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_pendingMutex
                        );


                    const auto found =
                        g_pendingRemovals.find(
                            userId
                        );


                    if (
                        found !=
                        g_pendingRemovals.end()
                        )
                    {
                        pending =
                            found->second;


                        g_pendingRemovals.erase(
                            found
                        );


                        foundPending =
                            true;
                    }
                }


                if (!foundPending)
                {
                    event.reply(
                        ephemeralText(
                            "❌ This removal request expired. "
                            "Run `/removeconfig` again."
                        )
                    );

                    return;
                }


                const ConfigRecord config =
                    findConfig(
                        database,
                        pending.publicId
                    );


                if (!config.found)
                {
                    event.reply(
                        ephemeralText(
                            "❌ That config could not be found."
                        )
                    );

                    return;
                }


                if (
                    config.visibility !=
                    "public"
                    )
                {
                    event.reply(
                        ephemeralText(
                            "⚠️ This config is already hidden."
                        )
                    );

                    return;
                }


                if (
                    !setVisibility(
                        database,
                        config.id,
                        "hidden"
                    )
                    )
                {
                    event.reply(
                        ephemeralText(
                            "❌ MX couldn't remove this config."
                        )
                    );

                    return;
                }


                addModerationHistory(
                    database,
                    config,
                    "removed",
                    pending.reason,
                    userId,
                    user.username
                );


                StorageService::log(
                    bot,
                    MXChannels::Storage::SECURITY_EVENTS,
                    "🗑️ Config Removed",

                    "**Config:** `" +
                    config.scriptName +
                    "`\n"
                    "**MX ID:** `" +
                    config.publicId +
                    "`\n"
                    "**Removed By:** `" +
                    user.username +
                    "`\n"
                    "**Reason:** " +
                    pending.reason
                );


                event.reply(
                    dpp::ir_update_message,

                    buildRemovedMessage(
                        config,
                        pending.reason
                    )
                );
            }
        );


        /*
         * ========================================================
         * EDIT CONFIG FORM SUBMISSION
         * ========================================================
         */

        bot.on_form_submit(
            [&bot, &database](
                const dpp::form_submit_t& event
                )
            {
                if (
                    !startsWith(
                        event.custom_id,
                        EDIT_MODAL_PREFIX
                    )
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
                        ephemeralText(
                            "❌ Only the MX Central owner "
                            "can edit configs."
                        )
                    );

                    return;
                }


                const std::string publicId =
                    event.custom_id.substr(
                        EDIT_MODAL_PREFIX.size()
                    );


                const ConfigRecord original =
                    findConfig(
                        database,
                        publicId
                    );


                if (!original.found)
                {
                    event.reply(
                        ephemeralText(
                            "❌ This config could no longer be found."
                        )
                    );

                    return;
                }


                const std::string enteredName =
                    getModalValue(
                        event,
                        "edit_name"
                    );


                const std::string enteredGame =
                    getModalValue(
                        event,
                        "edit_game"
                    );


                const std::string enteredPlatform =
                    getModalValue(
                        event,
                        "edit_platform"
                    );


                const std::string enteredSensitivity =
                    getModalValue(
                        event,
                        "edit_sensitivity"
                    );


                const std::string newName =
                    enteredName.empty()
                    ? original.scriptName
                    : enteredName;


                const std::string newGame =
                    enteredGame.empty()
                    ? original.game
                    : enteredGame;


                std::string newPlatform =
                    original.platform;


                if (
                    !enteredPlatform.empty()
                    )
                {
                    const std::string normalized =
                        normalizePlatform(
                            enteredPlatform
                        );


                    if (
                        normalized ==
                        "__INVALID__"
                        )
                    {
                        event.reply(
                            ephemeralText(
                                "❌ Platform must be one of:\n"
                                "`PS` • `Xbox` • `PC` • `Not Sure`"
                            )
                        );

                        return;
                    }


                    newPlatform =
                        normalized;
                }


                const std::string newSensitivity =
                    enteredSensitivity.empty()
                    ? original.sensitivity
                    : enteredSensitivity;


                if (
                    newName ==
                    original.scriptName &&
                    newGame ==
                    original.game &&
                    newPlatform ==
                    original.platform &&
                    newSensitivity ==
                    original.sensitivity
                    )
                {
                    event.reply(
                        ephemeralText(
                            "ℹ️ No changes were entered."
                        )
                    );

                    return;
                }


                const std::string changes =
                    buildEditSummary(
                        original,
                        newName,
                        newGame,
                        newPlatform,
                        newSensitivity
                    );


                if (
                    !updateConfigMetadata(
                        database,
                        original,
                        newName,
                        newGame,
                        newPlatform,
                        newSensitivity
                    )
                    )
                {
                    event.reply(
                        ephemeralText(
                            "❌ MX couldn't update this config."
                        )
                    );

                    return;
                }


                addModerationHistory(
                    database,
                    original,
                    "edited",
                    changes,
                    userId,
                    user.username
                );


                StorageService::log(
                    bot,
                    MXChannels::Storage::SYSTEM_LOGS,
                    "✏️ Config Edited",

                    "**MX ID:** `" +
                    original.publicId +
                    "`\n"
                    "**Edited By:** `" +
                    user.username +
                    "`\n\n" +
                    changes
                );


                StorageService::log(
                    bot,
                    MXChannels::Storage::SCRIPT_METADATA,
                    "✏️ Script Metadata Updated",

                    "**MX ID:** `" +
                    original.publicId +
                    "`\n"
                    "**Name:** `" +
                    newName +
                    "`\n"
                    "**Game:** `" +
                    newGame +
                    "`\n"
                    "**Platform:** `" +
                    newPlatform +
                    "`\n"
                    "**Sensitivity:** `" +
                    newSensitivity +
                    "`"
                );


                dpp::embed embed;

                embed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "✏️ Config Updated"
                    )

                    .set_description(
                        "**" +
                        newName +
                        "** has been updated successfully."
                    )

                    .add_field(
                        "MX ID",
                        "`" +
                        original.publicId +
                        "`",
                        true
                    )

                    .add_field(
                        "Game",
                        "`" +
                        newGame +
                        "`",
                        true
                    )

                    .add_field(
                        "Platform",
                        "`" +
                        newPlatform +
                        "`",
                        true
                    )

                    .add_field(
                        "Sensitivity",
                        "`" +
                        newSensitivity +
                        "`",
                        true
                    )

                    .add_field(
                        "Changes",
                        changes,
                        false
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Config Management"
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
    }


    /*
     * ============================================================
     * /removeconfig
     * ============================================================
     */

    void handleRemove(
        const dpp::slashcommand_t& event,
        Database& database
    )
    {
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
                ephemeralText(
                    "❌ `/removeconfig` is restricted "
                    "to the MX Central owner."
                )
            );

            return;
        }


        std::string publicId;
        std::string reason;


        try
        {
            publicId =
                std::get<std::string>(
                    event.get_parameter(
                        "mx-id"
                    )
                );


            reason =
                std::get<std::string>(
                    event.get_parameter(
                        "reason"
                    )
                );
        }
        catch (...)
        {
            event.reply(
                ephemeralText(
                    "❌ Invalid command information."
                )
            );

            return;
        }


        const ConfigRecord config =
            findConfig(
                database,
                publicId
            );


        if (!config.found)
        {
            event.reply(
                ephemeralText(
                    "❌ No config was found with MX ID `" +
                    publicId +
                    "`."
                )
            );

            return;
        }


        if (
            config.visibility !=
            "public"
            )
        {
            event.reply(
                ephemeralText(
                    "⚠️ `" +
                    config.publicId +
                    "` is already hidden.\n\n"
                    "Use `/restoreconfig` to restore it."
                )
            );

            return;
        }


        {
            std::lock_guard<std::mutex>
                lock(
                    g_pendingMutex
                );


            PendingRemoval pending;

            pending.publicId =
                config.publicId;

            pending.reason =
                reason;


            g_pendingRemovals[
                userId
            ] = pending;
        }


        event.reply(
            buildRemovalConfirmation(
                config,
                reason
            )
        );
    }


    /*
     * ============================================================
     * /restoreconfig
     * ============================================================
     */

    void handleRestore(
        const dpp::slashcommand_t& event,
        Database& database
    )
    {
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
                ephemeralText(
                    "❌ `/restoreconfig` is restricted "
                    "to the MX Central owner."
                )
            );

            return;
        }


        std::string publicId;


        try
        {
            publicId =
                std::get<std::string>(
                    event.get_parameter(
                        "mx-id"
                    )
                );
        }
        catch (...)
        {
            event.reply(
                ephemeralText(
                    "❌ Invalid MX ID."
                )
            );

            return;
        }


        const ConfigRecord config =
            findConfig(
                database,
                publicId
            );


        if (!config.found)
        {
            event.reply(
                ephemeralText(
                    "❌ No config was found with MX ID `" +
                    publicId +
                    "`."
                )
            );

            return;
        }


        if (
            config.visibility ==
            "public"
            )
        {
            event.reply(
                ephemeralText(
                    "ℹ️ `" +
                    config.publicId +
                    "` is already public."
                )
            );

            return;
        }


        if (
            !setVisibility(
                database,
                config.id,
                "public"
            )
            )
        {
            event.reply(
                ephemeralText(
                    "❌ MX couldn't restore this config."
                )
            );

            return;
        }


        addModerationHistory(
            database,
            config,
            "restored",
            "Restored by MX Central owner",
            userId,
            user.username
        );


        if (g_bot != nullptr)
        {
            StorageService::log(
                *g_bot,
                MXChannels::Storage::SECURITY_EVENTS,
                "♻️ Config Restored",

                "**Config:** `" +
                config.scriptName +
                "`\n"
                "**MX ID:** `" +
                config.publicId +
                "`\n"
                "**Restored By:** `" +
                user.username +
                "`"
            );
        }


        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "♻️ Config Restored"
            )

            .set_description(
                "**" +
                config.scriptName +
                "** is public again."
            )

            .add_field(
                "MX ID",
                "`" +
                config.publicId +
                "`",
                true
            )

            .add_field(
                "Status",
                "🟢 `Public`",
                true
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Config Management"
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
     * /editconfig
     * ============================================================
     */

    void handleEdit(
        const dpp::slashcommand_t& event,
        Database& database
    )
    {
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
                ephemeralText(
                    "❌ `/editconfig` is restricted "
                    "to the MX Central owner."
                )
            );

            return;
        }


        std::string publicId;


        try
        {
            publicId =
                std::get<std::string>(
                    event.get_parameter(
                        "mx-id"
                    )
                );
        }
        catch (...)
        {
            event.reply(
                ephemeralText(
                    "❌ Invalid MX ID."
                )
            );

            return;
        }


        const ConfigRecord config =
            findConfig(
                database,
                publicId
            );


        if (!config.found)
        {
            event.reply(
                ephemeralText(
                    "❌ No config was found with MX ID `" +
                    publicId +
                    "`."
                )
            );

            return;
        }


        /*
         * DPP text inputs need a valid text style.
         *
         * All fields are optional.
         * Leaving one blank keeps the current value.
         */

        dpp::interaction_modal_response modal(
            EDIT_MODAL_PREFIX +
            config.publicId,

            "Edit MX Config"
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Config Name"
            )
            .set_id(
                "edit_name"
            )
            .set_type(
                dpp::cot_text
            )
            .set_text_style(
                dpp::text_short
            )
            .set_placeholder(
                makePlaceholder(
                    config.scriptName
                )
            )
            .set_required(
                false
            )
            .set_max_length(
                100
            )
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Game"
            )
            .set_id(
                "edit_game"
            )
            .set_type(
                dpp::cot_text
            )
            .set_text_style(
                dpp::text_short
            )
            .set_placeholder(
                makePlaceholder(
                    config.game
                )
            )
            .set_required(
                false
            )
            .set_max_length(
                100
            )
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "Platform"
            )
            .set_id(
                "edit_platform"
            )
            .set_type(
                dpp::cot_text
            )
            .set_text_style(
                dpp::text_short
            )
            .set_placeholder(
                "PS / Xbox / PC / Not Sure"
            )
            .set_required(
                false
            )
            .set_max_length(
                50
            )
        );


        modal.add_component(
            dpp::component()
            .set_label(
                "In-Game Sensitivity"
            )
            .set_id(
                "edit_sensitivity"
            )
            .set_type(
                dpp::cot_text
            )
            .set_text_style(
                dpp::text_short
            )
            .set_placeholder(
                makePlaceholder(
                    config.sensitivity
                )
            )
            .set_required(
                false
            )
            .set_max_length(
                100
            )
        );


        event.dialog(
            modal
        );
    }
}