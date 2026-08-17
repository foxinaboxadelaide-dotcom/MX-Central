#include "commands/HelpCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include <sqlite3.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>


namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    const std::string HELP_PANEL_KEY =
        "help_panel";


    const std::string HELP_SELECT_ID =
        "mx_help_topic";


    Database* g_database =
        nullptr;


    dpp::cluster* g_bot =
        nullptr;


    std::atomic<bool> g_initialized{
        false
    };


    /*
     * ============================================================
     * SMALL HELPERS
     * ============================================================
     */

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


    std::string channelMention(
        std::uint64_t channelId
    )
    {
        return
            "<#" +
            std::to_string(
                channelId
            ) +
            ">";
    }


    /*
     * ============================================================
     * HELP SELECT MENU
     * ============================================================
     */

    dpp::component buildHelpMenu()
    {
        dpp::component menu;


        menu
            .set_type(
                dpp::cot_selectmenu
            )

            .set_id(
                HELP_SELECT_ID
            )

            .set_placeholder(
                "Choose a help topic..."
            )

            .set_min_values(
                1
            )

            .set_max_values(
                1
            );


        menu.add_select_option(
            dpp::select_option(
                "🔎 Finding Configs",
                "find",
                "Search by game, config name or MX ID"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "📋 Copying Configs",
                "copy",
                "Copy single and multipart Matrix configs"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "📤 Uploading Configs",
                "upload",
                "Share your own config with MX Central"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "⭐ Favorites",
                "favorites",
                "Save configs and access them later"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "👤 Creator Profiles",
                "creators",
                "Creator stats and verified creators"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "⭐ Ratings",
                "ratings",
                "Rate configs from 1 to 5 stars"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "🔥 Latest & Trending",
                "discover",
                "Discover new and popular configs"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "🎮 Platforms",
                "platforms",
                "PS, Xbox, PC and Not Sure"
            )
        );


        menu.add_select_option(
            dpp::select_option(
                "🟢 Status & Cooldowns",
                "status",
                "Bot status and anti-spam cooldowns"
            )
        );


        return menu;
    }


    /*
     * ============================================================
     * MAIN HELP EMBED
     * ============================================================
     */

    dpp::embed buildMainHelpEmbed()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🟣 MX Central • Help Centre"
            )

            .set_description(
                "Everything you need to search, copy, upload "
                "and manage configs through **MX Central**.\n\n"
                "Use the menu below for detailed instructions."
            )

            .add_field(
                "🔎 Find Configs",

                "Use `/find` in " +
                channelMention(
                    MXChannels::Main::FIND_CONFIG
                ) +
                " to search by **game**, **config name** "
                "or **MX ID**.",

                false
            )

            .add_field(
                "📤 Upload Configs",

                "Use `/upload-config` in " +
                channelMention(
                    MXChannels::Main::UPLOAD_CONFIG
                ) +
                " to submit your own Matrix config.",

                false
            )

            .add_field(
                "⭐ Favorites",

                "Save configs directly from their config panel, "
                "then use `/favorites` to access them again.",

                false
            )

            .add_field(
                "👤 Creator Profiles",

                "Use `/creator` to view creator uploads, downloads, "
                "favorites, ratings and their top configs.",

                false
            )

            .add_field(
                "✅ Verified Creators",

                "Verified creators have been manually recognised "
                "by MX Central and display a **Verified** badge.",

                false
            )

            .add_field(
                "⭐ Ratings",

                "Open any config and select **Rate Config** "
                "to give it a rating from **1–5 stars**.",

                false
            )

            .add_field(
                "🔥 Discover",

                "Check " +
                channelMention(
                    MXChannels::Main::LATEST_RELEASES
                ) +
                " for newly added configs and " +
                channelMention(
                    MXChannels::Main::TRENDING_CONFIGS
                ) +
                " for popular configs.",

                false
            )

            .add_field(
                "🟢 System Information",

                "Live statistics: " +
                channelMention(
                    MXChannels::Main::STATISTICS
                ) +
                "\n"
                "Bot status: " +
                channelMention(
                    MXChannels::Main::BOT_STATUS
                ),

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Select a topic below"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * MAIN HELP MESSAGE
     * ============================================================
     */

    dpp::message buildMainHelpMessage(
        bool ephemeral
    )
    {
        dpp::message message;


        message.add_embed(
            buildMainHelpEmbed()
        );


        dpp::component row;


        row.add_component(
            buildHelpMenu()
        );


        message.add_component(
            row
        );


        if (ephemeral)
        {
            message.set_flags(
                dpp::m_ephemeral
            );
        }


        return message;
    }


    /*
     * ============================================================
     * FIND TOPIC
     * ============================================================
     */

    dpp::embed buildFindHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🔎 Finding Configs"
            )

            .set_description(
                "MX Central's main search command is `/find`."
            )

            .add_field(
                "Where",

                "Use `/find` inside " +
                channelMention(
                    MXChannels::Main::FIND_CONFIG
                ) +
                ".",

                false
            )

            .add_field(
                "Search Options",

                "You can search using:\n"
                "• **Game name** — `Rust`\n"
                "• **Config name** — `Recoil V4`\n"
                "• **MX ID** — `MX-000014`",

                false
            )

            .add_field(
                "Search Results",

                "Successful searches are posted publicly so other "
                "members can see available configs.\n\n"
                "Selecting a result opens the actual config panel "
                "**privately for you**.",

                false
            )

            .add_field(
                "Tip",

                "Searching the exact MX ID is the quickest way "
                "to locate a specific config.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Search Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * COPY TOPIC
     * ============================================================
     */

    dpp::embed buildCopyHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "📋 Copying Configs"
            )

            .set_description(
                "Once you select a config, MX opens a private "
                "config panel with its copy controls."
            )

            .add_field(
                "Single-Part Config",

                "Press **Copy Config**.\n\n"
                "MX sends the exact raw Matrix config privately "
                "so you can use Discord's **Copy Text** option.",

                false
            )

            .add_field(
                "Multipart Config",

                "Multipart configs show buttons such as:\n"
                "`Part 1` • `Part 2` • `Part 3`\n\n"
                "Each button returns that exact part separately.",

                false
            )

            .add_field(
                "All Parts",

                "Press **All Parts** and MX sends every part as "
                "its own private message in the correct order.",

                false
            )

            .add_field(
                "Important",

                "MX does not add headers, code blocks or extra "
                "text to the raw config response.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Copy Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * UPLOAD TOPIC
     * ============================================================
     */

    dpp::embed buildUploadHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "📤 Uploading Configs"
            )

            .set_description(
                "Share your Matrix configs with the MX Central library."
            )

            .add_field(
                "Start",

                "Go to " +
                channelMention(
                    MXChannels::Main::UPLOAD_CONFIG
                ) +
                " and run `/upload-config`.",

                false
            )

            .add_field(
                "Information Required",

                "MX will ask for:\n"
                "• Config name\n"
                "• Game\n"
                "• Platform\n"
                "• In-game sensitivity\n"
                "• Number of config parts",

                false
            )

            .add_field(
                "Parts",

                "Configs can contain between **1 and 20 parts**.\n\n"
                "After starting the upload, paste each raw config "
                "part when MX asks for it.",

                false
            )

            .add_field(
                "Review",

                "Community uploads enter the MX approval queue.\n\n"
                "Once approved, the config becomes searchable and "
                "a new release embed is posted.",

                false
            )

            .add_field(
                "Supported Platforms",

                "`PS` • `Xbox` • `PC` • `Not Sure`",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Upload Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * FAVORITES TOPIC
     * ============================================================
     */

    dpp::embed buildFavoritesHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⭐ Favorites"
            )

            .set_description(
                "Favorites let you keep configs you regularly use."
            )

            .add_field(
                "Save a Config",

                "Open any config and press **⭐ Favorite**.\n\n"
                "MX saves the config to your Discord account.",

                false
            )

            .add_field(
                "View Favorites",

                "Run `/favorites` to open your private saved-config list.",

                false
            )

            .add_field(
                "Open a Favorite",

                "Select a saved config from the `/favorites` dropdown "
                "and the normal private config panel opens.",

                false
            )

            .add_field(
                "Remove a Favorite",

                "Press **⭐ Favorite** on a config you've already saved "
                "and MX removes it from your favorites.",

                false
            )

            .add_field(
                "Persistence",

                "Favorites are stored by MX and remain saved "
                "after the bot restarts.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Favorites Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * CREATOR TOPIC
     * ============================================================
     */

    dpp::embed buildCreatorHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "👤 Creator Profiles"
            )

            .set_description(
                "Creator profiles show the performance of people "
                "who upload configs to MX Central."
            )

            .add_field(
                "Your Profile",

                "Use `/creator` to view your own creator profile.",

                false
            )

            .add_field(
                "Another Creator",

                "Use `/creator user:@member` to view another creator.",

                false
            )

            .add_field(
                "Profile Statistics",

                "Profiles can display:\n"
                "• Public configs\n"
                "• Total downloads\n"
                "• Total favorites\n"
                "• Creator rating\n"
                "• Ratings received\n"
                "• Top configs",

                false
            )

            .add_field(
                "✅ Verified Creator",

                "A **Verified Creator** has been manually verified "
                "by MX Central.\n\n"
                "Their creator profile and config panels display "
                "the Verified badge.",

                false
            )

            .add_field(
                "Verification",

                "Verification is controlled manually by MX Central. "
                "There is no public verification command.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Creator Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * RATINGS TOPIC
     * ============================================================
     */

    dpp::embed buildRatingsHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "⭐ Config Ratings"
            )

            .set_description(
                "Ratings help the community identify high-quality configs."
            )

            .add_field(
                "Rate a Config",

                "Open a config and press **Rate Config**.",

                false
            )

            .add_field(
                "Rating Scale",

                "Choose between **1 and 5 stars**.",

                false
            )

            .add_field(
                "One Rating Per User",

                "Each Discord user has one active rating per config.",

                false
            )

            .add_field(
                "Change Your Rating",

                "You can rate the same config again later.\n\n"
                "Your previous rating will be updated rather than "
                "creating a duplicate rating.",

                false
            )

            .add_field(
                "Community Rating",

                "The config panel displays the current average rating "
                "and total number of ratings.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Ratings Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * DISCOVER TOPIC
     * ============================================================
     */

    dpp::embed buildDiscoverHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🔥 Latest & Trending"
            )

            .set_description(
                "There are several ways to discover configs "
                "without searching for a specific one."
            )

            .add_field(
                "🆕 Latest Releases",

                "Newly approved configs receive their own release embed in " +
                channelMention(
                    MXChannels::Main::LATEST_RELEASES
                ) +
                ".",

                false
            )

            .add_field(
                "🆕 /latest",

                "Use `/latest` to privately browse the newest "
                "configs in the library.",

                false
            )

            .add_field(
                "🔥 Trending",

                "Visit " +
                channelMention(
                    MXChannels::Main::TRENDING_CONFIGS
                ) +
                " or use `/trending` to discover popular configs.",

                false
            )

            .add_field(
                "How Trending Works",

                "Trending is influenced by config activity such as "
                "downloads and community ratings.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Discovery Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * PLATFORMS TOPIC
     * ============================================================
     */

    dpp::embed buildPlatformsHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🎮 Platforms & Compatibility"
            )

            .set_description(
                "MX Central separates configs by their intended platform."
            )

            .add_field(
                "Supported Platform Labels",

                "🎮 `PS`\n"
                "🎮 `Xbox`\n"
                "🖥️ `PC`\n"
                "❔ `Not Sure`",

                false
            )

            .add_field(
                "Why Platform Matters",

                "XIM Matrix configs can differ between PlayStation, "
                "Xbox and PC setups.\n\n"
                "Always check the platform shown on the config panel.",

                false
            )

            .add_field(
                "Sensitivity",

                "The uploader's in-game sensitivity is also displayed "
                "because matching sensitivity can be important when "
                "using somebody else's config.",

                false
            )

            .add_field(
                "Not Sure",

                "`Not Sure` means the uploader could not confidently "
                "identify the intended platform.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Platform Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * STATUS / COOLDOWN TOPIC
     * ============================================================
     */

    dpp::embed buildStatusHelp()
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🟢 Status & Command Cooldowns"
            )

            .set_description(
                "MX Central includes live service information and "
                "short anti-spam cooldowns."
            )

            .add_field(
                "Bot Status",

                "Check " +
                channelMention(
                    MXChannels::Main::BOT_STATUS
                ) +
                " for bot, database and service status.",

                false
            )

            .add_field(
                "Statistics",

                "Check " +
                channelMention(
                    MXChannels::Main::STATISTICS
                ) +
                " for live MX Central statistics.",

                false
            )

            .add_field(
                "Command Cooldowns",

                "`/find` — **3 seconds**\n"
                "`/favorites` — **3 seconds**\n"
                "`/creator` — **4 seconds**\n"
                "`/latest` — **5 seconds**\n"
                "`/trending` — **5 seconds**\n"
                "`/help` — **3 seconds**\n"
                "`/upload-config` — **10 seconds**",

                false
            )

            .add_field(
                "Why Cooldowns Exist",

                "Cooldowns help prevent spam and unnecessary load "
                "while keeping normal MX usage fast.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • System Help"
                )
            );


        return embed;
    }


    /*
     * ============================================================
     * TOPIC MESSAGE
     * ============================================================
     */

    dpp::message buildTopicMessage(
        const std::string& topic
    )
    {
        dpp::message message;


        if (
            topic ==
            "find"
            )
        {
            message.add_embed(
                buildFindHelp()
            );
        }
        else if (
            topic ==
            "copy"
            )
        {
            message.add_embed(
                buildCopyHelp()
            );
        }
        else if (
            topic ==
            "upload"
            )
        {
            message.add_embed(
                buildUploadHelp()
            );
        }
        else if (
            topic ==
            "favorites"
            )
        {
            message.add_embed(
                buildFavoritesHelp()
            );
        }
        else if (
            topic ==
            "creators"
            )
        {
            message.add_embed(
                buildCreatorHelp()
            );
        }
        else if (
            topic ==
            "ratings"
            )
        {
            message.add_embed(
                buildRatingsHelp()
            );
        }
        else if (
            topic ==
            "discover"
            )
        {
            message.add_embed(
                buildDiscoverHelp()
            );
        }
        else if (
            topic ==
            "platforms"
            )
        {
            message.add_embed(
                buildPlatformsHelp()
            );
        }
        else if (
            topic ==
            "status"
            )
        {
            message.add_embed(
                buildStatusHelp()
            );
        }
        else
        {
            message.set_content(
                "❌ That help topic is unavailable."
            );
        }


        message.set_flags(
            dpp::m_ephemeral
        );


        return message;
    }


    /*
     * ============================================================
     * LIVE MESSAGE DATABASE
     * ============================================================
     */

    void ensureLiveMessageTable()
    {
        if (
            g_database ==
            nullptr
            )
        {
            return;
        }


        g_database->execute(
            "CREATE TABLE IF NOT EXISTS live_messages ("
            "message_key TEXT PRIMARY KEY, "
            "channel_id TEXT NOT NULL, "
            "message_id TEXT NOT NULL, "
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
        );
    }


    std::uint64_t loadSavedMessageId()
    {
        if (
            g_database ==
            nullptr
            )
        {
            return 0;
        }


        sqlite3* database =
            g_database->handle();


        if (
            database ==
            nullptr
            )
        {
            return 0;
        }


        constexpr const char* sql =
            "SELECT message_id "
            "FROM live_messages "
            "WHERE message_key = ? "
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
            return 0;
        }


        sqlite3_bind_text(
            statement,
            1,
            HELP_PANEL_KEY.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        std::uint64_t messageId =
            0;


        if (
            sqlite3_step(
                statement
            ) ==
            SQLITE_ROW
            )
        {
            const unsigned char* value =
                sqlite3_column_text(
                    statement,
                    0
                );


            if (
                value !=
                nullptr
                )
            {
                try
                {
                    messageId =
                        std::stoull(
                            reinterpret_cast<
                            const char*
                            >(
                                value
                                )
                        );
                }
                catch (...)
                {
                    messageId =
                        0;
                }
            }
        }


        sqlite3_finalize(
            statement
        );


        return messageId;
    }


    void saveMessageId(
        std::uint64_t messageId
    )
    {
        if (
            g_database ==
            nullptr
            )
        {
            return;
        }


        sqlite3* database =
            g_database->handle();


        if (
            database ==
            nullptr
            )
        {
            return;
        }


        constexpr const char* sql =
            "INSERT INTO live_messages "
            "(message_key, channel_id, message_id, updated_at) "
            "VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
            "ON CONFLICT(message_key) DO UPDATE SET "
            "channel_id = excluded.channel_id, "
            "message_id = excluded.message_id, "
            "updated_at = CURRENT_TIMESTAMP;";


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
            return;
        }


        const std::string channelIdText =
            std::to_string(
                static_cast<std::uint64_t>(
                    MXChannels::Main::HELP
                    )
            );


        const std::string messageIdText =
            std::to_string(
                messageId
            );


        sqlite3_bind_text(
            statement,
            1,
            HELP_PANEL_KEY.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            2,
            channelIdText.c_str(),
            -1,
            SQLITE_TRANSIENT
        );


        sqlite3_bind_text(
            statement,
            3,
            messageIdText.c_str(),
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
     * CREATE HELP PANEL
     * ============================================================
     */

    void createHelpPanel()
    {
        if (
            g_bot ==
            nullptr
            )
        {
            return;
        }


        dpp::message message =
            buildMainHelpMessage(
                false
            );


        message.set_channel_id(
            MXChannels::Main::HELP
        );


        g_bot->message_create(
            message,

            [](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    std::cerr
                        << "[HelpCommand] Could not create help panel: "
                        << callback.get_error().message
                        << std::endl;


                    return;
                }


                const dpp::message& created =
                    std::get<dpp::message>(
                        callback.value
                    );


                saveMessageId(
                    static_cast<std::uint64_t>(
                        created.id
                        )
                );


                std::cout
                    << "[HelpCommand] Help Centre panel created."
                    << std::endl;
            }
        );
    }


    /*
     * ============================================================
     * REFRESH EXISTING HELP PANEL
     * ============================================================
     */

    void refreshHelpPanel()
    {
        if (
            g_bot ==
            nullptr ||
            g_database ==
            nullptr
            )
        {
            return;
        }


        ensureLiveMessageTable();


        const std::uint64_t messageId =
            loadSavedMessageId();


        /*
         * No saved panel yet.
         */

        if (
            messageId ==
            0
            )
        {
            createHelpPanel();


            return;
        }


        dpp::message message =
            buildMainHelpMessage(
                false
            );


        message.id =
            dpp::snowflake(
                messageId
            );


        message.set_channel_id(
            MXChannels::Main::HELP
        );


        /*
         * Edit the same persistent panel.
         *
         * If it was manually deleted, create a replacement.
         */

        g_bot->message_edit(
            message,

            [](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    createHelpPanel();


                    return;
                }


                std::cout
                    << "[HelpCommand] Help Centre panel refreshed."
                    << std::endl;
            }
        );
    }
}


/*
 * ================================================================
 * PUBLIC HELP COMMAND API
 * ================================================================
 */

namespace HelpCommand
{
    void initialize(
        Database& database,
        dpp::cluster& bot
    )
    {
        g_database =
            &database;


        g_bot =
            &bot;


        if (
            g_initialized.exchange(
                true
            )
            )
        {
            return;
        }


        ensureLiveMessageTable();


        /*
         * ========================================================
         * HELP TOPIC SELECT MENU
         * ========================================================
         */

        bot.on_select_click(
            [](
                const dpp::select_click_t& event
                )
            {
                if (
                    event.custom_id !=
                    HELP_SELECT_ID
                    )
                {
                    return;
                }


                if (
                    event.values.empty()
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Select a help topic."
                        )
                    );


                    return;
                }


                event.reply(
                    buildTopicMessage(
                        event.values[0]
                    )
                );
            }
        );


        /*
         * ========================================================
         * RESTORE / UPDATE PERMANENT PANEL ON STARTUP
         * ========================================================
         */

        bot.on_ready(
            [](
                const dpp::ready_t&
                )
            {
                if (
                    !dpp::run_once<
                    struct mx_help_panel_startup
                    >()
                    )
                {
                    return;
                }


                refreshHelpPanel();
            }
        );


        std::cout
            << "[HelpCommand] Help Centre system enabled."
            << std::endl;
    }


    /*
     * Compatibility wrapper.
     */

    void initialize(
        dpp::cluster& bot,
        Database& database
    )
    {
        initialize(
            database,
            bot
        );
    }


    /*
     * Compatibility wrapper if CommandRegistry uses this name.
     */

    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    )
    {
        initialize(
            database,
            bot
        );
    }


    /*
     * ============================================================
     * /help
     * ============================================================
     */

    void handle(
        const dpp::slashcommand_t& event
    )
    {
        /*
         * /help gives the user their own private copy of the
         * Help Centre without creating channel clutter.
         */

        event.reply(
            buildMainHelpMessage(
                true
            )
        );
    }
}