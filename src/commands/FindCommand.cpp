#include "commands/FindCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include "services/FavoritesService.h"
#include "services/RatingService.h"
#include "services/SearchService.h"
#include "services/StorageService.h"

#include <dpp/dpp.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <memory>
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

    constexpr int FIND_COOLDOWN_SECONDS =
        3;


    const std::string CONFIG_SELECT_MENU_ID =
        "mx_config_select";

    const std::string PART_PREFIX =
        "mx_part:";

    const std::string ALL_PREFIX =
        "mx_all:";

    const std::string RATE_OPEN_PREFIX =
        "mx_rate_open:";

    const std::string RATE_SELECT_PREFIX =
        "mx_rate:";

    const std::string FAVORITE_PREFIX =
        "mx_favorite:";


    std::unique_ptr<FavoritesService>
        g_favoritesService;


    /*
     * ============================================================
     * FIND COOLDOWN
     * ============================================================
     *
     * This cooldown is intentionally inside FindCommand instead
     * of relying on a later on_slashcommand listener.
     *
     * That guarantees the search never executes if the user is
     * still on cooldown.
     */

    std::unordered_map<
        std::uint64_t,
        std::chrono::steady_clock::time_point
    > g_findCooldowns;

    std::mutex g_findCooldownMutex;


    /*
     * ============================================================
     * BASIC HELPERS
     * ============================================================
     */

    bool startsWith(
        const std::string& value,
        const std::string& prefix
    )
    {
        return
            value.rfind(
                prefix,
                0
            ) ==
            0;
    }


    std::string toLower(
        const std::string& value
    )
    {
        std::string result =
            value;

        std::transform(
            result.begin(),
            result.end(),
            result.begin(),

            [](
                unsigned char character
                )
            {
                return static_cast<char>(
                    std::tolower(
                        character
                    )
                    );
            }
        );

        return result;
    }


    bool equalsIgnoreCase(
        const std::string& first,
        const std::string& second
    )
    {
        return
            toLower(first) ==
            toLower(second);
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


    dpp::message makeRawConfigMessage(
        const std::string& configData
    )
    {
        dpp::message message;

        message.set_content(
            configData
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
     * FIND COOLDOWN CHECK
     * ============================================================
     */

    bool allowFindCommand(
        const dpp::slashcommand_t& event
    )
    {
        /*
         * Main MX server only.
         */

        if (
            event.command.guild_id !=
            MXChannels::MAIN_SERVER_ID
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


        std::lock_guard<std::mutex>
            lock(
                g_findCooldownMutex
            );


        const auto existing =
            g_findCooldowns.find(
                userId
            );


        if (
            existing !=
            g_findCooldowns.end()
            )
        {
            const double elapsedSeconds =
                std::chrono::duration<double>(
                    now -
                    existing->second
                ).count();


            if (
                elapsedSeconds <
                static_cast<double>(
                    FIND_COOLDOWN_SECONDS
                    )
                )
            {
                const double remainingExact =
                    static_cast<double>(
                        FIND_COOLDOWN_SECONDS
                        ) -
                    elapsedSeconds;


                const int remaining =
                    std::max(
                        1,
                        static_cast<int>(
                            std::ceil(
                                remainingExact
                            )
                            )
                    );


                event.reply(
                    makeEphemeral(
                        "⏳ Slow down. You can use `/find` again in **" +
                        std::to_string(
                            remaining
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


                /*
                 * Stop any later slash-command listeners from
                 * touching this interaction.
                 */

                event.cancel_event();


                return false;
            }
        }


        /*
         * Start cooldown only after the command has been allowed.
         */

        g_findCooldowns[userId] =
            now;


        /*
         * Basic long-uptime cleanup.
         */

        if (
            g_findCooldowns.size() >
            5000
            )
        {
            const auto savedTime =
                g_findCooldowns[userId];


            g_findCooldowns.clear();


            g_findCooldowns[userId] =
                savedTime;
        }


        return true;
    }


    /*
     * ============================================================
     * RATING DISPLAY
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
            return "Not rated yet";
        }


        std::ostringstream stream;

        stream
            << std::fixed
            << std::setprecision(1)
            << average
            << "/5 • "
            << ratingCount
            << " rating";


        if (
            ratingCount !=
            1
            )
        {
            stream
                << "s";
        }


        return stream.str();
    }


    /*
     * ============================================================
     * VERIFIED CREATOR CHECK
     * ============================================================
     */

    bool isVerifiedCreator(
        SearchService& searchService,
        std::uint64_t creatorId
    )
    {
        if (
            creatorId ==
            0
            )
        {
            return false;
        }


        sqlite3* database =
            searchService.database().handle();


        if (
            database ==
            nullptr
            )
        {
            return false;
        }


        constexpr const char* sql =
            "SELECT COALESCE(is_verified, 0) "
            "FROM users "
            "WHERE discord_id = ? "
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


        const std::string creatorIdText =
            std::to_string(
                creatorId
            );


        sqlite3_bind_text(
            statement,
            1,
            creatorIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        bool verified =
            false;


        if (
            sqlite3_step(
                statement
            ) ==
            SQLITE_ROW
            )
        {
            verified =
                sqlite3_column_int(
                    statement,
                    0
                ) !=
                0;
        }


        sqlite3_finalize(
            statement
        );


        return verified;
    }


    /*
     * ============================================================
     * CREATOR DISPLAY
     * ============================================================
     */

    std::string creatorText(
        const SearchResult& result,
        SearchService& searchService
    )
    {
        std::string creator;


        if (
            result.creatorId !=
            0
            )
        {
            creator =
                "<@" +
                std::to_string(
                    result.creatorId
                ) +
                ">";
        }
        else if (
            !result.creatorName.empty()
            )
        {
            creator =
                "`" +
                result.creatorName +
                "`";
        }
        else
        {
            creator =
                "`Unknown`";
        }


        if (
            result.creatorId !=
            0 &&
            isVerifiedCreator(
                searchService,
                result.creatorId
            )
            )
        {
            creator +=
                " • ✅ **Verified**";
        }


        return creator;
    }


    std::string creatorDropdownText(
        const SearchResult& result
    )
    {
        if (
            !result.creatorName.empty()
            )
        {
            return
                "by " +
                result.creatorName;
        }


        if (
            result.creatorId !=
            0
            )
        {
            return
                "by Discord user";
        }


        return
            "uploader unknown";
    }


    std::string limitSelectDescription(
        const std::string& value
    )
    {
        constexpr std::size_t MAXIMUM =
            100;


        if (
            value.size() <=
            MAXIMUM
            )
        {
            return value;
        }


        return
            value.substr(
                0,
                MAXIMUM -
                3
            ) +
            "...";
    }


    /*
     * ============================================================
     * SEARCH SUMMARY
     * ============================================================
     */

    std::string resultWord(
        int amount
    )
    {
        return
            amount ==
            1
            ? "result"
            : "results";
    }


    std::string configWord(
        int amount
    )
    {
        return
            amount ==
            1
            ? "config"
            : "configs";
    }


    std::string buildSearchSummary(
        const std::vector<SearchResult>& results,
        const std::string& searchText
    )
    {
        int exactScriptMatches =
            0;

        int exactGameMatches =
            0;


        for (
            const SearchResult& result :
            results
            )
        {
            if (
                equalsIgnoreCase(
                    result.scriptName,
                    searchText
                )
                )
            {
                ++exactScriptMatches;
            }


            if (
                equalsIgnoreCase(
                    result.game,
                    searchText
                )
                )
            {
                ++exactGameMatches;
            }
        }


        const int totalResults =
            static_cast<int>(
                results.size()
                );


        if (
            exactScriptMatches >
            0
            )
        {
            std::string text =
                "🎯 Found **" +
                std::to_string(
                    exactScriptMatches
                ) +
                " exact script " +
                resultWord(
                    exactScriptMatches
                ) +
                "**";


            if (
                exactGameMatches >
                0
                )
            {
                text +=
                    " and **" +
                    std::to_string(
                        exactGameMatches
                    ) +
                    " exact game " +
                    resultWord(
                        exactGameMatches
                    ) +
                    "**";
            }


            text +=
                " for `" +
                searchText +
                "`.";


            return text;
        }


        if (
            exactGameMatches >
            0
            )
        {
            return
                "🎮 Found **" +
                std::to_string(
                    exactGameMatches
                ) +
                " " +
                configWord(
                    exactGameMatches
                ) +
                "** for the exact game `" +
                searchText +
                "`.";
        }


        return
            "🔎 Showing **" +
            std::to_string(
                totalResults
            ) +
            " similar " +
            resultWord(
                totalResults
            ) +
            "** for `" +
            searchText +
            "` below.";
    }


    /*
     * ============================================================
     * BUTTON ROW BUILDER
     * ============================================================
     */

    void addButtonToRows(
        dpp::message& message,
        std::vector<dpp::component>& buttons
    )
    {
        std::size_t index =
            0;


        while (
            index <
            buttons.size()
            )
        {
            dpp::component row;

            int rowCount =
                0;


            while (
                index <
                buttons.size() &&
                rowCount <
                5
                )
            {
                row.add_component(
                    buttons[index]
                );


                ++index;
                ++rowCount;
            }


            message.add_component(
                row
            );
        }
    }


    /*
     * ============================================================
     * CONFIG PANEL
     * ============================================================
     */

    dpp::message buildConfigPanel(
        const ScriptPartResult& result,
        SearchService& searchService
    )
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⚡ " +
                result.script.scriptName
            )

            .set_description(
                "Copy the config, save it to your favorites, "
                "or leave a rating."
            )

            .add_field(
                "MX ID",

                "`" +
                result.script.publicId +
                "`",

                true
            )

            .add_field(
                "Game",

                "`" +
                result.script.game +
                "`",

                true
            )

            .add_field(
                "Platform",

                "`" +
                result.script.platform +
                "`",

                true
            )

            .add_field(
                "Sensitivity",

                "`" +
                result.script.sensitivity +
                "`",

                true
            )

            .add_field(
                "Uploaded By",

                creatorText(
                    result.script,
                    searchService
                ),

                true
            )

            .add_field(
                "Parts",

                "`" +
                std::to_string(
                    result.parts.size()
                ) +
                "`",

                true
            )

            .add_field(
                "Rating",

                "`" +
                ratingText(
                    result.script.averageRating,
                    result.script.ratingCount
                ) +
                "`",

                true
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Config Library"
                )
            );


        dpp::message response;

        response.add_embed(
            embed
        );

        response.set_flags(
            dpp::m_ephemeral
        );


        std::vector<dpp::component>
            buttons;


        /*
         * SINGLE PART
         */

        if (
            result.parts.size() ==
            1
            )
        {
            buttons.push_back(
                dpp::component()
                .set_label(
                    "Copy Config"
                )

                .set_type(
                    dpp::cot_button
                )

                .set_style(
                    dpp::cos_primary
                )

                .set_id(
                    PART_PREFIX +
                    result.script.publicId +
                    ":1"
                )
            );
        }

        /*
         * MULTIPART
         */

        else
        {
            for (
                std::size_t index =
                0;

                index <
                result.parts.size();

                ++index
                )
            {
                buttons.push_back(
                    dpp::component()
                    .set_label(
                        "Part " +
                        std::to_string(
                            index +
                            1
                        )
                    )

                    .set_type(
                        dpp::cot_button
                    )

                    .set_style(
                        dpp::cos_primary
                    )

                    .set_id(
                        PART_PREFIX +
                        result.script.publicId +
                        ":" +
                        std::to_string(
                            index +
                            1
                        )
                    )
                );
            }


            buttons.push_back(
                dpp::component()
                .set_label(
                    "All Parts"
                )

                .set_type(
                    dpp::cot_button
                )

                .set_style(
                    dpp::cos_success
                )

                .set_id(
                    ALL_PREFIX +
                    result.script.publicId
                )
            );
        }


        /*
         * FAVORITE
         */

        buttons.push_back(
            dpp::component()
            .set_label(
                "⭐ Favorite"
            )

            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_secondary
            )

            .set_id(
                FAVORITE_PREFIX +
                result.script.publicId
            )
        );


        /*
         * RATE
         */

        buttons.push_back(
            dpp::component()
            .set_label(
                "Rate Config"
            )

            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_secondary
            )

            .set_id(
                RATE_OPEN_PREFIX +
                result.script.publicId
            )
        );


        addButtonToRows(
            response,
            buttons
        );


        return response;
    }


    /*
     * ============================================================
     * RATING MENU
     * ============================================================
     */

    dpp::message buildRatingMenu(
        const ScriptPartResult& result
    )
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⭐ Rate " +
                result.script.scriptName
            )

            .set_description(
                "Choose a rating from **1 to 5 stars**.\n\n"
                "You can change your rating later."
            )

            .add_field(
                "Current Rating",

                "`" +
                ratingText(
                    result.script.averageRating,
                    result.script.ratingCount
                ) +
                "`",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Ratings"
                )
            );


        dpp::component ratingMenu;


        ratingMenu
            .set_type(
                dpp::cot_selectmenu
            )

            .set_id(
                RATE_SELECT_PREFIX +
                result.script.publicId
            )

            .set_placeholder(
                "Choose your rating..."
            )

            .set_min_values(
                1
            )

            .set_max_values(
                1
            );


        ratingMenu.add_select_option(
            dpp::select_option(
                "⭐ 1 Star",
                "1",
                "Rate this config 1 out of 5"
            )
        );


        ratingMenu.add_select_option(
            dpp::select_option(
                "⭐⭐ 2 Stars",
                "2",
                "Rate this config 2 out of 5"
            )
        );


        ratingMenu.add_select_option(
            dpp::select_option(
                "⭐⭐⭐ 3 Stars",
                "3",
                "Rate this config 3 out of 5"
            )
        );


        ratingMenu.add_select_option(
            dpp::select_option(
                "⭐⭐⭐⭐ 4 Stars",
                "4",
                "Rate this config 4 out of 5"
            )
        );


        ratingMenu.add_select_option(
            dpp::select_option(
                "⭐⭐⭐⭐⭐ 5 Stars",
                "5",
                "Rate this config 5 out of 5"
            )
        );


        dpp::component row;

        row.add_component(
            ratingMenu
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


    /*
     * ============================================================
     * PART BUTTON PARSER
     * ============================================================
     */

    bool parsePartButton(
        const std::string& customId,
        std::string& publicId,
        int& partNumber
    )
    {
        if (
            !startsWith(
                customId,
                PART_PREFIX
            )
            )
        {
            return false;
        }


        const std::string payload =
            customId.substr(
                PART_PREFIX.length()
            );


        const std::size_t separator =
            payload.rfind(
                ':'
            );


        if (
            separator ==
            std::string::npos
            )
        {
            return false;
        }


        publicId =
            payload.substr(
                0,
                separator
            );


        try
        {
            partNumber =
                std::stoi(
                    payload.substr(
                        separator +
                        1
                    )
                );
        }
        catch (...)
        {
            return false;
        }


        return true;
    }
}


namespace FindCommand
{
    /*
     * ============================================================
     * /find
     * ============================================================
     */

    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    )
    {
        /*
         * Correct channel first.
         *
         * Using /find in the wrong channel does NOT start a
         * cooldown.
         */

        if (
            event.command.channel_id !=
            MXChannels::Main::FIND_CONFIG
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ Please use `/find` in <#" +
                    std::to_string(
                        MXChannels::Main::FIND_CONFIG
                    ) +
                    ">."
                )
            );


            return;
        }


        /*
         * ========================================================
         * REAL /find COOLDOWN
         * ========================================================
         */

        if (
            !allowFindCommand(
                event
            )
            )
        {
            return;
        }


        std::string searchText;


        try
        {
            searchText =
                std::get<std::string>(
                    event.get_parameter(
                        "script-or-game"
                    )
                );
        }
        catch (...)
        {
            event.reply(
                makeEphemeral(
                    "❌ Enter a script name or game."
                )
            );


            return;
        }


        if (
            searchText.empty()
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ Enter a script name or game."
                )
            );


            return;
        }


        const std::vector<SearchResult> results =
            searchService.search(
                searchText,
                5
            );


        if (
            results.empty()
            )
        {
            dpp::embed embed;


            embed
                .set_color(
                    MX_PURPLE
                )

                .set_title(
                    "🔎 No Configs Found"
                )

                .set_description(
                    "MX couldn't find anything matching:\n"
                    "`" +
                    searchText +
                    "`"
                )

                .add_field(
                    "Try Again",

                    "Try a different **script name** "
                    "or **game name**.",

                    false
                )

                .set_footer(
                    dpp::embed_footer()
                    .set_text(
                        "MX Central • Search"
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


        const dpp::user& searchUser =
            event.command.get_issuing_user();


        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🔎 MX Config Search"
            )

            .set_description(
                "**Search:** `" +
                searchText +
                "`\n"
                "**Searched by:** <@" +
                std::to_string(
                    static_cast<std::uint64_t>(
                        searchUser.id
                        )
                ) +
                ">\n\n" +
                buildSearchSummary(
                    results,
                    searchText
                )
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Choose a config below"
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
                "Choose a config..."
            )

            .set_min_values(
                1
            )

            .set_max_values(
                1
            );


        for (
            const SearchResult& result :
            results
            )
        {
            std::string label =
                result.scriptName +
                " • " +
                result.platform;


            if (
                label.size() >
                100
                )
            {
                label =
                    label.substr(
                        0,
                        97
                    ) +
                    "...";
            }


            const std::string description =
                limitSelectDescription(
                    result.game +
                    " • " +
                    std::to_string(
                        result.downloadCount
                    ) +
                    " downloads • " +
                    creatorDropdownText(
                        result
                    ) +
                    " • " +
                    result.publicId
                );


            selectMenu.add_select_option(
                dpp::select_option(
                    label,
                    result.publicId,
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


        /*
         * Successful search stays public.
         */

        event.reply(
            response
        );
    }


    /*
     * ============================================================
     * INTERACTION HANDLERS
     * ============================================================
     */

    void registerHandlers(
        dpp::cluster& bot,
        SearchService& searchService,
        RatingService& ratingService
    )
    {
        /*
         * ========================================================
         * FAVORITES / CREATOR SYSTEM
         * ========================================================
         */

        if (
            !g_favoritesService
            )
        {
            g_favoritesService =
                std::make_unique<FavoritesService>(
                    searchService
                );


            g_favoritesService
                ->registerHandlers(
                    bot
                );
        }


        /*
         * ========================================================
         * SELECT MENUS
         * ========================================================
         */

        bot.on_select_click(
            [
                &bot,
                &searchService,
                &ratingService
            ](
                const dpp::select_click_t& event
                )
            {
                /*
                 * ====================================================
                 * CONFIG SELECT
                 * ====================================================
                 *
                 * Used by:
                 *
                 * /find
                 * /latest
                 * /trending
                 * /favorites
                 */

                if (
                    event.custom_id ==
                    CONFIG_SELECT_MENU_ID
                    )
                {
                    if (
                        event.values.empty()
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ No config was selected."
                            )
                        );


                        return;
                    }


                    const std::string publicId =
                        event.values[0];


                    const ScriptPartResult result =
                        searchService.getScript(
                            publicId
                        );


                    if (
                        !result.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't open this config.\n\n"
                                "**Reason:** " +
                                result.error
                            )
                        );


                        return;
                    }


                    const dpp::user& user =
                        event.command.get_issuing_user();


                    const std::uint64_t userId =
                        static_cast<std::uint64_t>(
                            user.id
                            );


                    searchService.recordDownload(
                        publicId,
                        userId,
                        user.username
                    );


                    StorageService::logDownload(
                        bot,
                        user.username,

                        result.script.publicId +
                        " • " +
                        result.script.scriptName
                    );


                    event.reply(
                        buildConfigPanel(
                            result,
                            searchService
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * RATING SELECT
                 * ====================================================
                 */

                if (
                    startsWith(
                        event.custom_id,
                        RATE_SELECT_PREFIX
                    )
                    )
                {
                    if (
                        event.values.empty()
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ No rating was selected."
                            )
                        );


                        return;
                    }


                    const std::string publicId =
                        event.custom_id.substr(
                            RATE_SELECT_PREFIX.length()
                        );


                    int rating =
                        0;


                    try
                    {
                        rating =
                            std::stoi(
                                event.values[0]
                            );
                    }
                    catch (...)
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ Invalid rating."
                            )
                        );


                        return;
                    }


                    const dpp::user& user =
                        event.command.get_issuing_user();


                    const std::uint64_t userId =
                        static_cast<std::uint64_t>(
                            user.id
                            );


                    const RatingResult ratingResult =
                        ratingService.rateConfig(
                            publicId,
                            userId,
                            user.username,
                            rating
                        );


                    if (
                        !ratingResult.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't save your rating.\n\n"
                                "**Reason:** " +
                                ratingResult.error
                            )
                        );


                        return;
                    }


                    StorageService::logRating(
                        bot,
                        user.username,

                        ratingResult.publicId +
                        " • " +
                        ratingResult.scriptName,

                        ratingResult.rating
                    );


                    std::ostringstream averageText;


                    averageText
                        << std::fixed
                        << std::setprecision(1)
                        << ratingResult.averageRating
                        << "/5";


                    dpp::embed embed;


                    embed
                        .set_color(
                            MX_PURPLE
                        )

                        .set_title(
                            "⭐ Rating Saved"
                        )

                        .set_description(
                            "You rated **" +
                            ratingResult.scriptName +
                            "** **" +
                            std::to_string(
                                ratingResult.rating
                            ) +
                            "/5**."
                        )

                        .add_field(
                            "Community Rating",

                            "`" +
                            averageText.str() +
                            "`",

                            true
                        )

                        .add_field(
                            "Ratings",

                            "`" +
                            std::to_string(
                                ratingResult.ratingCount
                            ) +
                            "`",

                            true
                        )

                        .add_field(
                            "Status",

                            ratingResult.updatedExisting
                            ? "🔄 Previous rating updated"
                            : "✅ New rating added",

                            false
                        )

                        .set_footer(
                            dpp::embed_footer()
                            .set_text(
                                "MX Central • Ratings"
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
            }
        );


        /*
         * ========================================================
         * BUTTON HANDLERS
         * ========================================================
         */

        bot.on_button_click(
            [
                &bot,
                &searchService
            ](
                const dpp::button_click_t& event
                )
            {
                /*
                 * ====================================================
                 * FAVORITE
                 * ====================================================
                 */

                if (
                    startsWith(
                        event.custom_id,
                        FAVORITE_PREFIX
                    )
                    )
                {
                    if (
                        !g_favoritesService
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ Favorites are currently unavailable."
                            )
                        );


                        return;
                    }


                    const std::string publicId =
                        event.custom_id.substr(
                            FAVORITE_PREFIX.length()
                        );


                    const dpp::user& user =
                        event.command.get_issuing_user();


                    const std::uint64_t userId =
                        static_cast<std::uint64_t>(
                            user.id
                            );


                    const FavoriteToggleResult favoriteResult =
                        g_favoritesService
                        ->toggleFavorite(
                            publicId,
                            userId,
                            user.username
                        );


                    if (
                        !favoriteResult.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't update your favorites.\n\n"
                                "**Reason:** " +
                                favoriteResult.error
                            )
                        );


                        return;
                    }


                    dpp::embed embed;


                    embed
                        .set_color(
                            MX_PURPLE
                        )

                        .set_title(
                            favoriteResult.isFavorite
                            ? "⭐ Config Saved"
                            : "🗑️ Favorite Removed"
                        )

                        .set_description(
                            favoriteResult.isFavorite
                            ? (
                                "**" +
                                favoriteResult.scriptName +
                                "** was added to your favorites.\n\n"
                                "Use `/favorites` whenever you want "
                                "to open your saved configs."
                                )
                            : (
                                "**" +
                                favoriteResult.scriptName +
                                "** was removed from your favorites."
                                )
                        )

                        .add_field(
                            "MX ID",

                            "`" +
                            favoriteResult.publicId +
                            "`",

                            true
                        )

                        .add_field(
                            "Total Saves",

                            "`" +
                            std::to_string(
                                favoriteResult.favoriteCount
                            ) +
                            "`",

                            true
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


                /*
                 * ====================================================
                 * RATE CONFIG
                 * ====================================================
                 */

                if (
                    startsWith(
                        event.custom_id,
                        RATE_OPEN_PREFIX
                    )
                    )
                {
                    const std::string publicId =
                        event.custom_id.substr(
                            RATE_OPEN_PREFIX.length()
                        );


                    const ScriptPartResult result =
                        searchService.getScript(
                            publicId
                        );


                    if (
                        !result.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't load this config."
                            )
                        );


                        return;
                    }


                    event.reply(
                        buildRatingMenu(
                            result
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * INDIVIDUAL CONFIG PART
                 * ====================================================
                 */

                if (
                    startsWith(
                        event.custom_id,
                        PART_PREFIX
                    )
                    )
                {
                    std::string publicId;

                    int partNumber =
                        0;


                    if (
                        !parsePartButton(
                            event.custom_id,
                            publicId,
                            partNumber
                        )
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't determine "
                                "which part you selected."
                            )
                        );


                        return;
                    }


                    const ScriptPartResult result =
                        searchService.getScript(
                            publicId
                        );


                    if (
                        !result.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't load this config."
                            )
                        );


                        return;
                    }


                    if (
                        partNumber <
                        1 ||
                        partNumber >
                        static_cast<int>(
                            result.parts.size()
                            )
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ That config part doesn't exist."
                            )
                        );


                        return;
                    }


                    const std::string& configData =
                        result.parts[
                            static_cast<std::size_t>(
                                partNumber -
                                1
                                )
                        ];


                    event.reply(
                        makeRawConfigMessage(
                            configData
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * ALL PARTS
                 * ====================================================
                 */

                if (
                    startsWith(
                        event.custom_id,
                        ALL_PREFIX
                    )
                    )
                {
                    const std::string publicId =
                        event.custom_id.substr(
                            ALL_PREFIX.length()
                        );


                    const ScriptPartResult result =
                        searchService.getScript(
                            publicId
                        );


                    if (
                        !result.success
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't load this config."
                            )
                        );


                        return;
                    }


                    if (
                        result.parts.empty()
                        )
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ This config has no parts."
                            )
                        );


                        return;
                    }


                    /*
                     * First part is the original interaction response.
                     */

                    event.reply(
                        makeRawConfigMessage(
                            result.parts[0]
                        )
                    );


                    /*
                     * Remaining parts are each sent privately as
                     * their own follow-up message.
                     */

                    for (
                        std::size_t index =
                        1;

                        index <
                        result.parts.size();

                        ++index
                        )
                    {
                        dpp::message partMessage =
                            makeRawConfigMessage(
                                result.parts[index]
                            );


                        bot.interaction_followup_create(
                            event.command.token,
                            partMessage
                        );
                    }


                    return;
                }
            }
        );
    }
}