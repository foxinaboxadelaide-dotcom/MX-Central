#include "services/FavoritesService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>


namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    /*
     * ============================================================
     * COMMAND NAMES
     * ============================================================
     */

    const std::string FAVORITES_COMMAND =
        "favorites";

    const std::string CREATOR_COMMAND =
        "creator";

    const std::string VERIFY_CREATOR_COMMAND =
        "verifycreator";

    const std::string UNVERIFY_CREATOR_COMMAND =
        "unverifycreator";

    const std::string CONFIG_SELECT_MENU_ID =
        "mx_config_select";


    /*
     * ============================================================
     * STARTUP
     * ============================================================
     */

    constexpr uint64_t COMMAND_REGISTER_DELAY_SECONDS =
        3;

    constexpr uint64_t COMMAND_CLEANUP_DELAY_SECONDS =
        8;


    /*
     * ============================================================
     * COOLDOWNS
     * ============================================================
     */

    std::unordered_map<
        std::string,
        std::chrono::steady_clock::time_point
    > g_commandCooldowns;


    std::mutex g_cooldownMutex;


    /*
     * ============================================================
     * OWNER COMMANDS
     * ============================================================
     */

    const std::unordered_set<std::string>
        OWNER_COMMANDS =
    {
        "addscript",
        "removeconfig",
        "restoreconfig",
        "editconfig",
        "replaceparts",
        "backupnow",
        "exportconfigs",
        "verifycreator",
        "unverifycreator"
    };


    /*
     * ============================================================
     * CREATOR PROFILE MODELS
     * ============================================================
     */

    struct CreatorConfigSummary
    {
        std::string publicId;
        std::string scriptName;
        std::string game;
        std::string platform;

        int downloads = 0;
        int favorites = 0;

        double averageRating = 0.0;
        int ratingCount = 0;
    };


    struct CreatorProfile
    {
        bool exists = false;
        bool verified = false;
        bool markedCreator = false;

        std::uint64_t userId = 0;

        std::string username;

        int configCount = 0;

        long long totalDownloads = 0;
        long long totalFavorites = 0;

        double averageRating = 0.0;
        int ratingCount = 0;

        std::vector<CreatorConfigSummary>
            topConfigs;
    };


    /*
     * ============================================================
     * SQLITE HELPERS
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


        if (
            value ==
            nullptr
            )
        {
            return "";
        }


        return reinterpret_cast<const char*>(
            value
            );
    }


    std::uint64_t columnSnowflake(
        sqlite3_stmt* statement,
        int column
    )
    {
        const std::string value =
            columnText(
                statement,
                column
            );


        if (value.empty())
        {
            return 0;
        }


        try
        {
            return std::stoull(
                value
            );
        }
        catch (...)
        {
            return 0;
        }
    }


    SearchResult readSearchResult(
        sqlite3_stmt* statement
    )
    {
        SearchResult result;


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


        result.scriptName =
            columnText(
                statement,
                2
            );


        result.game =
            columnText(
                statement,
                3
            );


        result.platform =
            columnText(
                statement,
                4
            );


        result.sensitivity =
            columnText(
                statement,
                5
            );


        result.creatorId =
            columnSnowflake(
                statement,
                6
            );


        result.creatorName =
            columnText(
                statement,
                7
            );


        result.downloadCount =
            sqlite3_column_int(
                statement,
                8
            );


        result.averageRating =
            sqlite3_column_double(
                statement,
                9
            );


        result.ratingCount =
            sqlite3_column_int(
                statement,
                10
            );


        return result;
    }


    /*
     * ============================================================
     * DISPLAY HELPERS
     * ============================================================
     */

    std::string ratingText(
        double average,
        int ratingCount
    )
    {
        if (
            ratingCount <=
            0
            )
        {
            return "Not rated";
        }


        std::ostringstream stream;


        stream
            << std::fixed
            << std::setprecision(1)
            << average
            << "/5";


        return stream.str();
    }


    std::string truncateText(
        const std::string& value,
        std::size_t maximum
    )
    {
        if (
            value.size() <=
            maximum
            )
        {
            return value;
        }


        if (
            maximum <=
            3
            )
        {
            return value.substr(
                0,
                maximum
            );
        }


        return
            value.substr(
                0,
                maximum - 3
            ) +
            "...";
    }


    dpp::message makeEphemeral(
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


    /*
     * ============================================================
     * OWNER CHECK
     * ============================================================
     */

    bool isMainServerOwner(
        std::uint64_t userId
    )
    {
        dpp::guild* guild =
            dpp::find_guild(
                MXChannels::MAIN_SERVER_ID
            );


        if (
            guild ==
            nullptr
            )
        {
            return false;
        }


        return
            static_cast<std::uint64_t>(
                guild->owner_id
                ) ==
            userId;
    }


    /*
     * ============================================================
     * COMMAND COOLDOWNS
     * ============================================================
     */

    int cooldownSeconds(
        const std::string& command
    )
    {
        if (
            command ==
            "find"
            )
        {
            return 3;
        }


        if (
            command ==
            "favorites"
            )
        {
            return 3;
        }


        if (
            command ==
            "creator"
            )
        {
            return 4;
        }


        if (
            command ==
            "latest"
            )
        {
            return 5;
        }


        if (
            command ==
            "trending"
            )
        {
            return 5;
        }


        if (
            command ==
            "help"
            )
        {
            return 3;
        }


        if (
            command ==
            "upload-config"
            )
        {
            return 10;
        }


        return 0;
    }


    std::string cooldownKey(
        std::uint64_t userId,
        const std::string& command
    )
    {
        return
            std::to_string(
                userId
            ) +
            ":" +
            command;
    }


    bool checkCooldown(
        const dpp::slashcommand_t& event
    )
    {
        if (
            event.command.guild_id !=
            MXChannels::MAIN_SERVER_ID
            )
        {
            return true;
        }


        const std::string command =
            event.command.get_command_name();


        const int cooldown =
            cooldownSeconds(
                command
            );


        if (
            cooldown <=
            0
            )
        {
            return true;
        }


        const dpp::user& user =
            event.command.get_issuing_user();


        const std::uint64_t userId =
            static_cast<std::uint64_t>(
                user.id
                );


        /*
         * Owner bypass.
         */

        if (
            isMainServerOwner(
                userId
            )
            )
        {
            return true;
        }


        const auto now =
            std::chrono::steady_clock::now();


        const std::string key =
            cooldownKey(
                userId,
                command
            );


        std::lock_guard<std::mutex>
            lock(
                g_cooldownMutex
            );


        const auto iterator =
            g_commandCooldowns.find(
                key
            );


        if (
            iterator !=
            g_commandCooldowns.end()
            )
        {
            const double elapsed =
                std::chrono::duration<double>(
                    now -
                    iterator->second
                ).count();


            if (
                elapsed <
                static_cast<double>(
                    cooldown
                    )
                )
            {
                const int remaining =
                    static_cast<int>(
                        std::ceil(
                            static_cast<double>(
                                cooldown
                                ) -
                            elapsed
                        )
                        );


                event.reply(
                    makeEphemeral(
                        "⏳ Slow down. You can use `/" +
                        command +
                        "` again in **" +
                        std::to_string(
                            std::max(
                                remaining,
                                1
                            )
                        ) +
                        " second" +
                        (
                            remaining ==
                            1
                            ? ""
                            : "s"
                            ) +
                        "**."
                    )
                );


                return false;
            }
        }


        g_commandCooldowns[key] =
            now;


        if (
            g_commandCooldowns.size() >
            5000
            )
        {
            g_commandCooldowns.clear();


            g_commandCooldowns[key] =
                now;
        }


        return true;
    }


    /*
     * ============================================================
     * CREATOR CONFIG COUNT
     * ============================================================
     */

    int creatorConfigCount(
        Database& databaseManager,
        std::uint64_t userId
    )
    {
        sqlite3* database =
            databaseManager.handle();


        if (
            database ==
            nullptr
            )
        {
            return 0;
        }


        constexpr const char* sql =
            "SELECT COUNT(*) "
            "FROM scripts "
            "WHERE CAST(creator_id AS TEXT) = ? "
            "AND visibility = 'public';";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) !=
            SQLITE_OK
            )
        {
            return 0;
        }


        const std::string userIdText =
            std::to_string(
                userId
            );


        sqlite3_bind_text(
            statement,
            1,
            userIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        int count =
            0;


        if (
            sqlite3_step(
                statement
            ) ==
            SQLITE_ROW
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
     * SET CREATOR VERIFICATION
     * ============================================================
     */

    bool setCreatorVerification(
        Database& databaseManager,
        std::uint64_t userId,
        bool verified,
        std::string& error
    )
    {
        sqlite3* database =
            databaseManager.handle();


        if (
            database ==
            nullptr
            )
        {
            error =
                "MX database is currently unavailable.";


            return false;
        }


        const std::string userIdText =
            std::to_string(
                userId
            );


        /*
         * Verification requires the user to actually have
         * at least one public config.
         */

        if (
            verified &&
            creatorConfigCount(
                databaseManager,
                userId
            ) <=
            0
            )
        {
            error =
                "That user does not currently have any public "
                "configs in MX Central.";


            return false;
        }


        /*
         * VERIFY
         *
         * INSERT handles older configs whose creator may exist in
         * scripts but doesn't yet have a users row.
         */

        if (verified)
        {
            constexpr const char* sql =
                "INSERT INTO users "
                "("
                "discord_id, "
                "username, "
                "is_creator, "
                "is_verified"
                ") "
                "VALUES (?, 'Unknown', 1, 1) "
                "ON CONFLICT(discord_id) DO UPDATE SET "
                "is_creator = 1, "
                "is_verified = 1, "
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
                ) !=
                SQLITE_OK
                )
            {
                error =
                    "MX couldn't prepare the verification update.";


                return false;
            }


            sqlite3_bind_text(
                statement,
                1,
                userIdText.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            const bool success =
                sqlite3_step(
                    statement
                ) ==
                SQLITE_DONE;


            sqlite3_finalize(
                statement
            );


            if (!success)
            {
                error =
                    "MX couldn't verify this creator.";


                return false;
            }


            return true;
        }


        /*
         * UNVERIFY
         */

        constexpr const char* sql =
            "UPDATE users "
            "SET "
            "is_verified = 0, "
            "last_seen_at = CURRENT_TIMESTAMP "
            "WHERE discord_id = ?;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                sql,
                -1,
                &statement,
                nullptr
            ) !=
            SQLITE_OK
            )
        {
            error =
                "MX couldn't prepare the verification update.";


            return false;
        }


        sqlite3_bind_text(
            statement,
            1,
            userIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        const bool success =
            sqlite3_step(
                statement
            ) ==
            SQLITE_DONE;


        const int affected =
            sqlite3_changes(
                database
            );


        sqlite3_finalize(
            statement
        );


        if (!success)
        {
            error =
                "MX couldn't remove creator verification.";


            return false;
        }


        if (
            affected <=
            0
            )
        {
            error =
                "That creator does not have an MX user record.";


            return false;
        }


        return true;
    }


    /*
     * ============================================================
     * CREATOR VERIFICATION COMMAND
     * ============================================================
     */

    void handleVerificationCommand(
        dpp::cluster& bot,
        Database& database,
        const dpp::slashcommand_t& event,
        bool verified
    )
    {
        const dpp::user& issuingUser =
            event.command.get_issuing_user();


        const std::uint64_t issuerId =
            static_cast<std::uint64_t>(
                issuingUser.id
                );


        /*
         * Runtime owner check.
         *
         * Even if an administrator manually gains visibility
         * to the Discord command, they still cannot execute it.
         */

        if (
            !isMainServerOwner(
                issuerId
            )
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ This command is restricted to the "
                    "MX Central server owner."
                )
            );


            return;
        }


        const dpp::command_value parameter =
            event.get_parameter(
                "user"
            );


        if (
            !std::holds_alternative<
            dpp::snowflake
            >(
                parameter
            )
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ Select a Discord user."
                )
            );


            return;
        }


        const std::uint64_t targetId =
            static_cast<std::uint64_t>(
                std::get<dpp::snowflake>(
                    parameter
                )
                );


        std::string error;


        if (
            !setCreatorVerification(
                database,
                targetId,
                verified,
                error
            )
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ MX couldn't update this creator.\n\n"
                    "**Reason:** " +
                    error
                )
            );


            return;
        }


        /*
         * ========================================================
         * STORAGE LOG
         * ========================================================
         */

        StorageService::log(
            bot,
            MXChannels::Storage::SYSTEM_LOGS,

            verified
            ? "✅ Creator Verified"
            : "🟣 Creator Unverified",

            "**Creator:** <@" +
            std::to_string(
                targetId
            ) +
            ">\n"
            "**Discord ID:** `" +
            std::to_string(
                targetId
            ) +
            "`\n"
            "**Actioned By:** <@" +
            std::to_string(
                issuerId
            ) +
            ">"
        );


        /*
         * ========================================================
         * CONFIRMATION
         * ========================================================
         */

        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                verified
                ? "✅ Creator Verified"
                : "🟣 Verification Removed"
            )

            .set_description(
                verified
                ? (
                    "<@" +
                    std::to_string(
                        targetId
                    ) +
                    "> is now a **Verified MX Creator**."
                    )
                : (
                    "<@" +
                    std::to_string(
                        targetId
                    ) +
                    "> is no longer a verified creator."
                    )
            )

            .add_field(
                "Creator",

                "<@" +
                std::to_string(
                    targetId
                ) +
                ">",

                true
            )

            .add_field(
                "Status",

                verified
                ? "✅ Verified"
                : "🟣 Standard Creator",

                true
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Creator Management"
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
     * LOAD CREATOR PROFILE
     * ============================================================
     */

    CreatorProfile loadCreatorProfile(
        Database& databaseManager,
        std::uint64_t userId
    )
    {
        CreatorProfile profile;


        profile.userId =
            userId;


        sqlite3* database =
            databaseManager.handle();


        if (
            database ==
            nullptr
            )
        {
            return profile;
        }


        const std::string userIdText =
            std::to_string(
                userId
            );


        /*
         * USER RECORD
         */

        constexpr const char* userSql =
            "SELECT "
            "COALESCE(username, ''), "
            "COALESCE(is_creator, 0), "
            "COALESCE(is_verified, 0) "
            "FROM users "
            "WHERE discord_id = ? "
            "LIMIT 1;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                userSql,
                -1,
                &statement,
                nullptr
            ) ==
            SQLITE_OK
            )
        {
            sqlite3_bind_text(
                statement,
                1,
                userIdText.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            if (
                sqlite3_step(
                    statement
                ) ==
                SQLITE_ROW
                )
            {
                profile.exists =
                    true;


                profile.username =
                    columnText(
                        statement,
                        0
                    );


                profile.markedCreator =
                    sqlite3_column_int(
                        statement,
                        1
                    ) !=
                    0;


                profile.verified =
                    sqlite3_column_int(
                        statement,
                        2
                    ) !=
                    0;
            }


            sqlite3_finalize(
                statement
            );
        }


        /*
         * TOTALS
         */

        constexpr const char* totalsSql =
            "SELECT "
            "COUNT(*), "
            "COALESCE(SUM(download_count), 0), "
            "COALESCE(SUM(favourite_count), 0) "
            "FROM scripts "
            "WHERE CAST(creator_id AS TEXT) = ? "
            "AND visibility = 'public';";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                totalsSql,
                -1,
                &statement,
                nullptr
            ) ==
            SQLITE_OK
            )
        {
            sqlite3_bind_text(
                statement,
                1,
                userIdText.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            if (
                sqlite3_step(
                    statement
                ) ==
                SQLITE_ROW
                )
            {
                profile.configCount =
                    sqlite3_column_int(
                        statement,
                        0
                    );


                profile.totalDownloads =
                    sqlite3_column_int64(
                        statement,
                        1
                    );


                profile.totalFavorites =
                    sqlite3_column_int64(
                        statement,
                        2
                    );
            }


            sqlite3_finalize(
                statement
            );
        }


        if (
            profile.configCount >
            0
            )
        {
            profile.exists =
                true;


            profile.markedCreator =
                true;
        }


        /*
         * CREATOR RATING
         */

        constexpr const char* ratingSql =
            "SELECT "
            "COALESCE(AVG(r.rating), 0), "
            "COUNT(r.rating) "
            "FROM ratings r "
            "INNER JOIN scripts s "
            "ON s.id = r.script_id "
            "WHERE CAST(s.creator_id AS TEXT) = ? "
            "AND s.visibility = 'public';";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                ratingSql,
                -1,
                &statement,
                nullptr
            ) ==
            SQLITE_OK
            )
        {
            sqlite3_bind_text(
                statement,
                1,
                userIdText.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            if (
                sqlite3_step(
                    statement
                ) ==
                SQLITE_ROW
                )
            {
                profile.averageRating =
                    sqlite3_column_double(
                        statement,
                        0
                    );


                profile.ratingCount =
                    sqlite3_column_int(
                        statement,
                        1
                    );
            }


            sqlite3_finalize(
                statement
            );
        }


        /*
         * TOP 5 CONFIGS
         */

        constexpr const char* topConfigsSql =
            "SELECT "
            "s.public_id, "
            "s.script_name, "
            "s.game, "
            "s.platform, "
            "s.download_count, "
            "s.favourite_count, "
            "COALESCE(summary.average_rating, 0), "
            "COALESCE(summary.rating_count, 0) "
            "FROM scripts s "
            "LEFT JOIN script_rating_summary summary "
            "ON summary.script_id = s.id "
            "WHERE CAST(s.creator_id AS TEXT) = ? "
            "AND s.visibility = 'public' "
            "ORDER BY "
            "s.download_count DESC, "
            "s.favourite_count DESC, "
            "COALESCE(summary.average_rating, 0) DESC, "
            "s.created_at DESC "
            "LIMIT 5;";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                topConfigsSql,
                -1,
                &statement,
                nullptr
            ) ==
            SQLITE_OK
            )
        {
            sqlite3_bind_text(
                statement,
                1,
                userIdText.c_str(),
                -1,
                SQLITE_TRANSIENT
            );


            while (
                sqlite3_step(
                    statement
                ) ==
                SQLITE_ROW
                )
            {
                CreatorConfigSummary config;


                config.publicId =
                    columnText(
                        statement,
                        0
                    );


                config.scriptName =
                    columnText(
                        statement,
                        1
                    );


                config.game =
                    columnText(
                        statement,
                        2
                    );


                config.platform =
                    columnText(
                        statement,
                        3
                    );


                config.downloads =
                    sqlite3_column_int(
                        statement,
                        4
                    );


                config.favorites =
                    sqlite3_column_int(
                        statement,
                        5
                    );


                config.averageRating =
                    sqlite3_column_double(
                        statement,
                        6
                    );


                config.ratingCount =
                    sqlite3_column_int(
                        statement,
                        7
                    );


                profile.topConfigs.push_back(
                    config
                );
            }


            sqlite3_finalize(
                statement
            );
        }


        return profile;
    }


    /*
     * ============================================================
     * /creator
     * ============================================================
     */

    void handleCreatorCommand(
        Database& database,
        const dpp::slashcommand_t& event
    )
    {
        const dpp::user& issuingUser =
            event.command.get_issuing_user();


        std::uint64_t targetUserId =
            static_cast<std::uint64_t>(
                issuingUser.id
                );


        const dpp::command_value userParameter =
            event.get_parameter(
                "user"
            );


        if (
            std::holds_alternative<dpp::snowflake>(
                userParameter
            )
            )
        {
            targetUserId =
                static_cast<std::uint64_t>(
                    std::get<dpp::snowflake>(
                        userParameter
                    )
                    );
        }


        const CreatorProfile profile =
            loadCreatorProfile(
                database,
                targetUserId
            );


        if (
            profile.configCount <=
            0
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ <@" +
                    std::to_string(
                        targetUserId
                    ) +
                    "> doesn't currently have any public "
                    "configs in MX Central."
                )
            );


            return;
        }


        std::string status;


        if (
            profile.verified
            )
        {
            status =
                "✅ **Verified Creator**";
        }
        else
        {
            status =
                "🟣 **MX Creator**";
        }


        std::string topConfigsText;


        for (
            std::size_t index =
            0;

            index <
            profile.topConfigs.size();

            ++index
            )
        {
            const CreatorConfigSummary& config =
                profile.topConfigs[index];


            topConfigsText +=
                "**" +
                std::to_string(
                    index +
                    1
                ) +
                ". " +
                config.scriptName +
                "** • `" +
                config.publicId +
                "`\n"
                "> " +
                config.game +
                " • " +
                config.platform +
                " • 📥 " +
                std::to_string(
                    config.downloads
                ) +
                " • ⭐ " +
                ratingText(
                    config.averageRating,
                    config.ratingCount
                ) +
                " • 💜 " +
                std::to_string(
                    config.favorites
                );


            if (
                index +
                1 <
                profile.topConfigs.size()
                )
            {
                topConfigsText +=
                    "\n\n";
            }
        }


        if (
            topConfigsText.empty()
            )
        {
            topConfigsText =
                "No public configs.";
        }


        std::ostringstream creatorRating;


        if (
            profile.ratingCount >
            0
            )
        {
            creatorRating
                << std::fixed
                << std::setprecision(1)
                << profile.averageRating
                << "/5";
        }
        else
        {
            creatorRating
                << "Not rated";
        }


        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                profile.verified
                ? "✅ Verified MX Creator"
                : "👤 MX Creator Profile"
            )

            .set_description(
                "Creator profile for <@" +
                std::to_string(
                    targetUserId
                ) +
                ">"
            )

            .add_field(
                "Status",
                status,
                true
            )

            .add_field(
                "Public Configs",

                "`" +
                std::to_string(
                    profile.configCount
                ) +
                "`",

                true
            )

            .add_field(
                "Total Downloads",

                "`" +
                std::to_string(
                    profile.totalDownloads
                ) +
                "`",

                true
            )

            .add_field(
                "Total Favorites",

                "`" +
                std::to_string(
                    profile.totalFavorites
                ) +
                "`",

                true
            )

            .add_field(
                "Creator Rating",

                "`" +
                creatorRating.str() +
                "`",

                true
            )

            .add_field(
                "Ratings Received",

                "`" +
                std::to_string(
                    profile.ratingCount
                ) +
                "`",

                true
            )

            .add_field(
                "🏆 Top Configs",
                topConfigsText,
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    profile.verified
                    ? "MX Central • Verified Creator"
                    : "MX Central • Creator Profiles"
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
     * OWNER COMMAND VISIBILITY CLEANUP
     * ============================================================
     */

    void cleanOwnerCommands(
        dpp::cluster& bot
    )
    {
        bot.guild_commands_get(
            MXChannels::MAIN_SERVER_ID,

            [&bot](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    std::cerr
                        << "[CommandCleanup] Could not load commands: "
                        << callback.get_error().message
                        << std::endl;


                    return;
                }


                const dpp::slashcommand_map commands =
                    callback.get<dpp::slashcommand_map>();


                int protectedCommands =
                    0;


                for (
                    const auto& entry :
                    commands
                    )
                {
                    const dpp::slashcommand& existing =
                        entry.second;


                    if (
                        !OWNER_COMMANDS.contains(
                            existing.name
                        )
                        )
                    {
                        continue;
                    }


                    dpp::slashcommand updated =
                        existing;


                    updated.set_default_permissions(
                        0
                    );


                    bot.guild_command_edit(
                        updated,
                        MXChannels::MAIN_SERVER_ID,

                        [
                            name =
                                existing.name
                        ](
                            const dpp::confirmation_callback_t&
                            editCallback
                            )
                        {
                            if (
                                editCallback.is_error()
                                )
                            {
                                std::cerr
                                    << "[CommandCleanup] Could not protect /"
                                    << name
                                    << ": "
                                    << editCallback.get_error().message
                                    << std::endl;


                                return;
                            }


                            std::cout
                                << "[CommandCleanup] Protected /"
                                << name
                                << std::endl;
                        }
                                );


                    ++protectedCommands;
                }


                std::cout
                    << "[CommandCleanup] "
                    << protectedCommands
                    << " owner commands queued for protection."
                    << std::endl;
            }
        );
    }
}


/*
 * ================================================================
 * FAVORITES SERVICE
 * ================================================================
 */


FavoritesService::FavoritesService(
    SearchService& searchService
)
    : m_searchService(
        searchService
    ),
    m_database(
        searchService.database()
    )
{
    ensureSchema();
}


/*
 * ================================================================
 * DATABASE SCHEMA
 * ================================================================
 */

bool FavoritesService::ensureSchema()
{
    const bool tableReady =
        m_database.execute(
            "CREATE TABLE IF NOT EXISTS favourites ("
            "script_id INTEGER NOT NULL, "
            "user_id TEXT NOT NULL, "
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, "
            "PRIMARY KEY (script_id, user_id)"
            ");"
        );


    if (!tableReady)
    {
        std::cerr
            << "[FavoritesService] Could not prepare favourites table."
            << std::endl;


        return false;
    }


    m_database.execute(
        "CREATE INDEX IF NOT EXISTS "
        "idx_favourites_user_created "
        "ON favourites(user_id, created_at DESC);"
    );


    return true;
}


/*
 * ================================================================
 * ENSURE USER
 * ================================================================
 */

bool FavoritesService::ensureUser(
    std::uint64_t userId,
    const std::string& username
)
{
    sqlite3* database =
        m_database.handle();


    if (
        database ==
        nullptr
        )
    {
        return false;
    }


    constexpr const char* sql =
        "INSERT INTO users "
        "(discord_id, username) "
        "VALUES (?, ?) "
        "ON CONFLICT(discord_id) DO UPDATE SET "
        "username = excluded.username, "
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
        ) !=
        SQLITE_OK
        )
    {
        return false;
    }


    const std::string userIdText =
        std::to_string(
            userId
        );


    sqlite3_bind_text(
        statement,
        1,
        userIdText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    sqlite3_bind_text(
        statement,
        2,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    const bool success =
        sqlite3_step(
            statement
        ) ==
        SQLITE_DONE;


    sqlite3_finalize(
        statement
    );


    return success;
}


/*
 * ================================================================
 * IS FAVORITE
 * ================================================================
 */

bool FavoritesService::isFavorite(
    const std::string& publicId,
    std::uint64_t userId
)
{
    if (
        publicId.empty() ||
        userId ==
        0
        )
    {
        return false;
    }


    sqlite3* database =
        m_database.handle();


    if (
        database ==
        nullptr
        )
    {
        return false;
    }


    constexpr const char* sql =
        "SELECT 1 "
        "FROM favourites f "
        "INNER JOIN scripts s "
        "ON s.id = f.script_id "
        "WHERE s.public_id = ? "
        "AND s.visibility = 'public' "
        "AND f.user_id = ? "
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
        ) !=
        SQLITE_OK
        )
    {
        return false;
    }


    const std::string userIdText =
        std::to_string(
            userId
        );


    sqlite3_bind_text(
        statement,
        1,
        publicId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    sqlite3_bind_text(
        statement,
        2,
        userIdText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    const bool found =
        sqlite3_step(
            statement
        ) ==
        SQLITE_ROW;


    sqlite3_finalize(
        statement
    );


    return found;
}


/*
 * ================================================================
 * TOGGLE FAVORITE
 * ================================================================
 */

FavoriteToggleResult FavoritesService::toggleFavorite(
    const std::string& publicId,
    std::uint64_t userId,
    const std::string& username
)
{
    FavoriteToggleResult result;


    result.publicId =
        publicId;


    if (publicId.empty())
    {
        result.error =
            "The MX ID was empty.";


        return result;
    }


    if (
        userId ==
        0
        )
    {
        result.error =
            "MX couldn't determine your Discord account.";


        return result;
    }


    if (!ensureSchema())
    {
        result.error =
            "The favorites database is unavailable.";


        return result;
    }


    if (
        !ensureUser(
            userId,
            username
        )
        )
    {
        result.error =
            "MX couldn't update your user record.";


        return result;
    }


    sqlite3* database =
        m_database.handle();


    if (
        database ==
        nullptr
        )
    {
        result.error =
            "MX database is currently unavailable.";


        return result;
    }


    constexpr const char* scriptSql =
        "SELECT id, script_name "
        "FROM scripts "
        "WHERE public_id = ? "
        "AND visibility = 'public' "
        "LIMIT 1;";


    sqlite3_stmt* statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            scriptSql,
            -1,
            &statement,
            nullptr
        ) !=
        SQLITE_OK
        )
    {
        result.error =
            "MX couldn't prepare the favorite action.";


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
        sqlite3_step(
            statement
        ) !=
        SQLITE_ROW
        )
    {
        sqlite3_finalize(
            statement
        );


        result.error =
            "This config is no longer available.";


        return result;
    }


    const long long scriptId =
        sqlite3_column_int64(
            statement,
            0
        );


    result.scriptName =
        columnText(
            statement,
            1
        );


    sqlite3_finalize(
        statement
    );


    if (
        !m_database.execute(
            "BEGIN IMMEDIATE TRANSACTION;"
        )
        )
    {
        result.error =
            "MX couldn't start the favorite update.";


        return result;
    }


    const std::string userIdText =
        std::to_string(
            userId
        );


    constexpr const char* checkSql =
        "SELECT 1 "
        "FROM favourites "
        "WHERE script_id = ? "
        "AND user_id = ? "
        "LIMIT 1;";


    statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            checkSql,
            -1,
            &statement,
            nullptr
        ) !=
        SQLITE_OK
        )
    {
        m_database.execute(
            "ROLLBACK;"
        );


        result.error =
            "MX couldn't check your favorites.";


        return result;
    }


    sqlite3_bind_int64(
        statement,
        1,
        scriptId
    );


    sqlite3_bind_text(
        statement,
        2,
        userIdText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    const bool alreadyFavorite =
        sqlite3_step(
            statement
        ) ==
        SQLITE_ROW;


    sqlite3_finalize(
        statement
    );


    if (alreadyFavorite)
    {
        constexpr const char* deleteSql =
            "DELETE FROM favourites "
            "WHERE script_id = ? "
            "AND user_id = ?;";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                deleteSql,
                -1,
                &statement,
                nullptr
            ) !=
            SQLITE_OK
            )
        {
            m_database.execute(
                "ROLLBACK;"
            );


            result.error =
                "MX couldn't remove this favorite.";


            return result;
        }


        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        sqlite3_bind_text(
            statement,
            2,
            userIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        const bool deleted =
            sqlite3_step(
                statement
            ) ==
            SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        if (!deleted)
        {
            m_database.execute(
                "ROLLBACK;"
            );


            result.error =
                "MX couldn't remove this favorite.";


            return result;
        }


        result.isFavorite =
            false;
    }
    else
    {
        constexpr const char* insertSql =
            "INSERT INTO favourites "
            "(script_id, user_id) "
            "VALUES (?, ?);";


        statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                insertSql,
                -1,
                &statement,
                nullptr
            ) !=
            SQLITE_OK
            )
        {
            m_database.execute(
                "ROLLBACK;"
            );


            result.error =
                "MX couldn't save this favorite.";


            return result;
        }


        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        sqlite3_bind_text(
            statement,
            2,
            userIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        const bool inserted =
            sqlite3_step(
                statement
            ) ==
            SQLITE_DONE;


        sqlite3_finalize(
            statement
        );


        if (!inserted)
        {
            m_database.execute(
                "ROLLBACK;"
            );


            result.error =
                "MX couldn't save this favorite.";


            return result;
        }


        result.isFavorite =
            true;
    }


    /*
     * UPDATE TOTAL FAVORITE COUNT
     */

    constexpr const char* countSql =
        "UPDATE scripts "
        "SET favourite_count = ("
        "SELECT COUNT(*) "
        "FROM favourites "
        "WHERE script_id = ?"
        ") "
        "WHERE id = ?;";


    statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            countSql,
            -1,
            &statement,
            nullptr
        ) !=
        SQLITE_OK
        )
    {
        m_database.execute(
            "ROLLBACK;"
        );


        result.error =
            "MX couldn't update the favorite counter.";


        return result;
    }


    sqlite3_bind_int64(
        statement,
        1,
        scriptId
    );


    sqlite3_bind_int64(
        statement,
        2,
        scriptId
    );


    const bool countUpdated =
        sqlite3_step(
            statement
        ) ==
        SQLITE_DONE;


    sqlite3_finalize(
        statement
    );


    if (!countUpdated)
    {
        m_database.execute(
            "ROLLBACK;"
        );


        result.error =
            "MX couldn't update the favorite counter.";


        return result;
    }


    constexpr const char* readCountSql =
        "SELECT COALESCE(favourite_count, 0) "
        "FROM scripts "
        "WHERE id = ? "
        "LIMIT 1;";


    statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            readCountSql,
            -1,
            &statement,
            nullptr
        ) ==
        SQLITE_OK
        )
    {
        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );


        if (
            sqlite3_step(
                statement
            ) ==
            SQLITE_ROW
            )
        {
            result.favoriteCount =
                sqlite3_column_int(
                    statement,
                    0
                );
        }


        sqlite3_finalize(
            statement
        );
    }


    if (
        !m_database.execute(
            "COMMIT;"
        )
        )
    {
        m_database.execute(
            "ROLLBACK;"
        );


        result.error =
            "MX couldn't finish the favorite update.";


        return result;
    }


    result.success =
        true;


    return result;
}


/*
 * ================================================================
 * GET FAVORITES
 * ================================================================
 */

std::vector<SearchResult> FavoritesService::getFavorites(
    std::uint64_t userId,
    int limit
)
{
    std::vector<SearchResult> results;


    if (
        userId ==
        0
        )
    {
        return results;
    }


    if (
        limit <
        1
        )
    {
        limit =
            1;
    }


    if (
        limit >
        25
        )
    {
        limit =
            25;
    }


    if (!ensureSchema())
    {
        return results;
    }


    sqlite3* database =
        m_database.handle();


    if (
        database ==
        nullptr
        )
    {
        return results;
    }


    constexpr const char* sql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "s.creator_id, "
        "COALESCE(u.username, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0) "
        "FROM favourites f "
        "INNER JOIN scripts s "
        "ON s.id = f.script_id "
        "LEFT JOIN users u "
        "ON u.discord_id = CAST(s.creator_id AS TEXT) "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id "
        "WHERE f.user_id = ? "
        "AND s.visibility = 'public' "
        "ORDER BY "
        "f.created_at DESC, "
        "s.id DESC "
        "LIMIT ?;";


    sqlite3_stmt* statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            sql,
            -1,
            &statement,
            nullptr
        ) !=
        SQLITE_OK
        )
    {
        std::cerr
            << "[FavoritesService] Favorites prepare failed: "
            << sqlite3_errmsg(
                database
            )
            << std::endl;


        return results;
    }


    const std::string userIdText =
        std::to_string(
            userId
        );


    sqlite3_bind_text(
        statement,
        1,
        userIdText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    sqlite3_bind_int(
        statement,
        2,
        limit
    );


    while (
        sqlite3_step(
            statement
        ) ==
        SQLITE_ROW
        )
    {
        results.push_back(
            readSearchResult(
                statement
            )
        );
    }


    sqlite3_finalize(
        statement
    );


    return results;
}


/*
 * ================================================================
 * /favorites
 * ================================================================
 */

void FavoritesService::handleFavoritesCommand(
    const dpp::slashcommand_t& event
)
{
    const dpp::user& user =
        event.command.get_issuing_user();


    const std::uint64_t userId =
        static_cast<std::uint64_t>(
            user.id
            );


    const std::vector<SearchResult> favorites =
        getFavorites(
            userId,
            25
        );


    if (favorites.empty())
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⭐ My Favorites"
            )

            .set_description(
                "You haven't saved any configs yet.\n\n"
                "Use `/find`, open a config, then press "
                "**⭐ Favorite** to save it here."
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Favorites"
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


        return;
    }


    dpp::embed embed;


    embed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "⭐ My Favorite Configs"
        )

        .set_description(
            "You have **" +
            std::to_string(
                favorites.size()
            ) +
            " saved config" +
            (
                favorites.size() ==
                1
                ? ""
                : "s"
                ) +
            "** shown below.\n\n"
            "Choose one from the dropdown to open it privately."
        );


    for (
        std::size_t index =
        0;

        index <
        favorites.size();

        ++index
        )
    {
        const SearchResult& config =
            favorites[index];


        embed.add_field(
            "#" +
            std::to_string(
                index +
                1
            ) +
            " • " +
            config.scriptName,

            "**Game:** `" +
            config.game +
            "`\n"
            "**Platform:** `" +
            config.platform +
            "`\n"
            "**MX ID:** `" +
            config.publicId +
            "`\n"
            "**Downloads:** `" +
            std::to_string(
                config.downloadCount
            ) +
            "`\n"
            "**Rating:** `" +
            ratingText(
                config.averageRating,
                config.ratingCount
            ) +
            "`",

            false
        );
    }


    embed.set_footer(
        dpp::embed_footer()
        .set_text(
            "MX Central • Favorites"
        )
    );


    dpp::component selectMenu;


    selectMenu
        .set_type(
            dpp::cot_selectmenu
        )

        .set_id(
            CONFIG_SELECT_MENU_ID
        )

        .set_placeholder(
            "Choose a saved config..."
        )

        .set_min_values(
            1
        )

        .set_max_values(
            1
        );


    for (
        const SearchResult& config :
        favorites
        )
    {
        const std::string label =
            truncateText(
                config.scriptName +
                " • " +
                config.platform,
                100
            );


        const std::string description =
            truncateText(
                config.game +
                " • " +
                config.publicId +
                " • " +
                std::to_string(
                    config.downloadCount
                ) +
                " downloads",
                100
            );


        selectMenu.add_select_option(
            dpp::select_option(
                label,
                config.publicId,
                description
            )
        );
    }


    dpp::component row;


    row.add_component(
        selectMenu
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


    event.reply(
        response
    );
}


/*
 * ================================================================
 * REGISTER HANDLERS
 * ================================================================
 */

void FavoritesService::registerHandlers(
    dpp::cluster& bot
)
{
    if (
        m_handlersRegistered
        )
    {
        return;
    }


    m_handlersRegistered =
        true;


    ensureSchema();


    /*
     * ============================================================
     * COOLDOWNS
     * ============================================================
     */

    bot.on_slashcommand(
        [](
            const dpp::slashcommand_t& event
            )
        {
            if (
                !checkCooldown(
                    event
                )
                )
            {
                event.cancel_event();
            }
        }
    );


    /*
     * ============================================================
     * COMMAND REGISTRATION
     * ============================================================
     */

    bot.on_ready(
        [&bot](
            const dpp::ready_t&
            )
        {
            if (
                !dpp::run_once<
                struct mx_creator_system_startup
                >()
                )
            {
                return;
            }


            bot.start_timer(
                [&bot](
                    const dpp::timer& timer
                    )
                {
                    /*
                     * /favorites
                     */

                    dpp::slashcommand favoritesCommand(
                        FAVORITES_COMMAND,
                        "View your saved MX Central configs",
                        bot.me.id
                    );


                    bot.guild_command_create(
                        favoritesCommand,
                        MXChannels::MAIN_SERVER_ID
                    );


                    /*
                     * /creator
                     */

                    dpp::slashcommand creatorCommand(
                        CREATOR_COMMAND,
                        "View an MX Central creator profile",
                        bot.me.id
                    );


                    creatorCommand.add_option(
                        dpp::command_option(
                            dpp::co_user,
                            "user",
                            "Creator to view (leave blank for yourself)",
                            false
                        )
                    );


                    bot.guild_command_create(
                        creatorCommand,
                        MXChannels::MAIN_SERVER_ID
                    );


                    /*
                     * /verifycreator
                     */

                    dpp::slashcommand verifyCommand(
                        VERIFY_CREATOR_COMMAND,
                        "Owner: verify an MX Central creator",
                        bot.me.id
                    );


                    verifyCommand.set_default_permissions(
                        0
                    );


                    verifyCommand.add_option(
                        dpp::command_option(
                            dpp::co_user,
                            "user",
                            "Creator to verify",
                            true
                        )
                    );


                    bot.guild_command_create(
                        verifyCommand,
                        MXChannels::MAIN_SERVER_ID,

                        [](
                            const dpp::confirmation_callback_t& callback
                            )
                        {
                            if (
                                callback.is_error()
                                )
                            {
                                std::cerr
                                    << "[CreatorVerification] Could not register "
                                    << "/verifycreator: "
                                    << callback.get_error().message
                                    << std::endl;
                            }
                            else
                            {
                                std::cout
                                    << "[CreatorVerification] /verifycreator registered."
                                    << std::endl;
                            }
                        }
                    );


                    /*
                     * /unverifycreator
                     */

                    dpp::slashcommand unverifyCommand(
                        UNVERIFY_CREATOR_COMMAND,
                        "Owner: remove MX creator verification",
                        bot.me.id
                    );


                    unverifyCommand.set_default_permissions(
                        0
                    );


                    unverifyCommand.add_option(
                        dpp::command_option(
                            dpp::co_user,
                            "user",
                            "Creator to unverify",
                            true
                        )
                    );


                    bot.guild_command_create(
                        unverifyCommand,
                        MXChannels::MAIN_SERVER_ID,

                        [](
                            const dpp::confirmation_callback_t& callback
                            )
                        {
                            if (
                                callback.is_error()
                                )
                            {
                                std::cerr
                                    << "[CreatorVerification] Could not register "
                                    << "/unverifycreator: "
                                    << callback.get_error().message
                                    << std::endl;
                            }
                            else
                            {
                                std::cout
                                    << "[CreatorVerification] /unverifycreator registered."
                                    << std::endl;
                            }
                        }
                    );


                    bot.stop_timer(
                        timer
                    );
                },

                COMMAND_REGISTER_DELAY_SECONDS
            );


            /*
             * OWNER COMMAND CLEANUP
             */

            bot.start_timer(
                [&bot](
                    const dpp::timer& timer
                    )
                {
                    cleanOwnerCommands(
                        bot
                    );


                    bot.stop_timer(
                        timer
                    );
                },

                COMMAND_CLEANUP_DELAY_SECONDS
            );
        }
    );


    /*
     * ============================================================
     * SLASH COMMAND HANDLER
     * ============================================================
     */

    bot.on_slashcommand(
        [
            this,
            &bot
        ](
            const dpp::slashcommand_t& event
            )
        {
            const std::string command =
                event.command.get_command_name();


            /*
             * /favorites
             */

            if (
                command ==
                FAVORITES_COMMAND
                )
            {
                if (
                    event.command.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ `/favorites` is only available "
                            "inside MX Central."
                        )
                    );


                    return;
                }


                handleFavoritesCommand(
                    event
                );


                return;
            }


            /*
             * /creator
             */

            if (
                command ==
                CREATOR_COMMAND
                )
            {
                if (
                    event.command.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ `/creator` is only available "
                            "inside MX Central."
                        )
                    );


                    return;
                }


                handleCreatorCommand(
                    m_database,
                    event
                );


                return;
            }


            /*
             * /verifycreator
             */

            if (
                command ==
                VERIFY_CREATOR_COMMAND
                )
            {
                handleVerificationCommand(
                    bot,
                    m_database,
                    event,
                    true
                );


                return;
            }


            /*
             * /unverifycreator
             */

            if (
                command ==
                UNVERIFY_CREATOR_COMMAND
                )
            {
                handleVerificationCommand(
                    bot,
                    m_database,
                    event,
                    false
                );


                return;
            }
        }
    );


    std::cout
        << "[CommandCooldown] Public command cooldowns enabled."
        << std::endl;


    std::cout
        << "[CreatorVerification] Creator verification system enabled."
        << std::endl;
}