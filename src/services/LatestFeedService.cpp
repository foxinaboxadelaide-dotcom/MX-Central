#include "services/LatestFeedService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/SearchService.h"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    /*
     * ============================================================
     * GLOBAL STATE
     * ============================================================
     */

    Database* g_database =
        nullptr;


    dpp::cluster* g_bot =
        nullptr;


    std::atomic<bool> g_initialized{
        false
    };


    std::mutex g_refreshMutex;


    long long g_lastSeenScriptId =
        0;


    /*
     * ============================================================
     * RATING TEXT
     * ============================================================
     */

    std::string ratingText(
        double average,
        int count
    )
    {
        if (count <= 0)
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


    /*
     * ============================================================
     * CREATOR
     * ============================================================
     */

    std::string creatorText(
        const SearchResult& config
    )
    {
        if (
            config.creatorId !=
            0
            )
        {
            return
                "<@" +
                std::to_string(
                    config.creatorId
                ) +
                ">";
        }


        if (
            !config.creatorName.empty()
            )
        {
            return
                "`" +
                config.creatorName +
                "`";
        }


        return "`Unknown`";
    }


    /*
     * ============================================================
     * REMOVE OLD LIVE PANEL
     * ============================================================
     *
     * The previous latest-releases system stored its persistent
     * Discord message ID inside live_messages.
     *
     * Since we are going back to individual release embeds, remove
     * that old dashboard once when MX starts.
     */

    void removeOldLivePanel()
    {
        if (
            g_database ==
            nullptr ||
            g_bot ==
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


        constexpr const char* selectSql =
            "SELECT "
            "channel_id, "
            "message_id "
            "FROM live_messages "
            "WHERE message_key = 'latest_releases_feed' "
            "LIMIT 1;";


        sqlite3_stmt* statement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                selectSql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return;
        }


        std::uint64_t channelId =
            0;


        std::uint64_t messageId =
            0;


        if (
            sqlite3_step(
                statement
            ) ==
            SQLITE_ROW
            )
        {
            const unsigned char* channelText =
                sqlite3_column_text(
                    statement,
                    0
                );


            const unsigned char* messageText =
                sqlite3_column_text(
                    statement,
                    1
                );


            if (
                channelText !=
                nullptr
                )
            {
                try
                {
                    channelId =
                        std::stoull(
                            reinterpret_cast<
                            const char*
                            >(
                                channelText
                                )
                        );
                }
                catch (...)
                {
                    channelId =
                        0;
                }
            }


            if (
                messageText !=
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
                                messageText
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


        /*
         * Delete the actual old Discord panel.
         */

        if (
            channelId !=
            0 &&
            messageId !=
            0
            )
        {
            g_bot->message_delete(
                messageId,
                channelId,

                [](
                    const dpp::confirmation_callback_t&
                    )
                {
                    /*
                     * No problem if the panel was already
                     * manually deleted.
                     */
                }
            );
        }


        /*
         * Remove the saved dashboard record.
         */

        g_database->execute(
            "DELETE FROM live_messages "
            "WHERE message_key = 'latest_releases_feed';"
        );
    }


    /*
     * ============================================================
     * SEED CURRENT SCRIPT ID
     * ============================================================
     *
     * Prevent old configs from being reposted every time the bot
     * restarts.
     */

    void seedCurrentLatest()
    {
        if (
            g_database ==
            nullptr
            )
        {
            return;
        }


        SearchService searchService(
            *g_database
        );


        const std::vector<SearchResult> latest =
            searchService.latest(
                1
            );


        if (
            latest.empty()
            )
        {
            g_lastSeenScriptId =
                0;


            return;
        }


        g_lastSeenScriptId =
            latest.front().id;
    }


    /*
     * ============================================================
     * POST NEW RELEASE EMBED
     * ============================================================
     */

    void postRelease(
        const SearchResult& config
    )
    {
        if (
            g_bot ==
            nullptr
            )
        {
            return;
        }


        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🆕 New MX Config"
            )

            .set_description(
                "**" +
                config.scriptName +
                "** has been added to MX Central.\n\n"
                "Use `/find` to locate and copy this config."
            )

            .add_field(
                "🎮 Game",

                "`" +
                config.game +
                "`",

                true
            )

            .add_field(
                "🕹️ Platform",

                "`" +
                config.platform +
                "`",

                true
            )

            .add_field(
                "🎯 Sensitivity",

                "`" +
                config.sensitivity +
                "`",

                true
            )

            .add_field(
                "👤 Uploaded By",

                creatorText(
                    config
                ),

                true
            )

            .add_field(
                "📥 Downloads",

                "`" +
                std::to_string(
                    config.downloadCount
                ) +
                "`",

                true
            )

            .add_field(
                "⭐ Rating",

                "`" +
                ratingText(
                    config.averageRating,
                    config.ratingCount
                ) +
                "`",

                true
            )

            .add_field(
                "🆔 MX ID",

                "`" +
                config.publicId +
                "`",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Latest Release"
                )
            );


        dpp::message message;


        message.set_channel_id(
            MXChannels::Main::LATEST_RELEASES
        );


        message.add_embed(
            embed
        );


        g_bot->message_create(
            message,

            [
                scriptName =
                    config.scriptName,

                    publicId =
                    config.publicId

            ](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    std::cerr
                        << "[LatestRelease] Failed to post "
                        << publicId
                        << " ("
                        << scriptName
                        << "): "
                        << callback.get_error().message
                        << std::endl;


                    return;
                }


                std::cout
                    << "[LatestRelease] Posted "
                    << publicId
                    << " • "
                    << scriptName
                    << std::endl;
            }
                    );
    }
}


namespace LatestFeedService
{
    /*
     * ============================================================
     * INITIALIZE
     * ============================================================
     */

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


        /*
         * Remember the newest config that already exists.
         *
         * That way restarting MX does not repost every existing
         * config into #latest-releases.
         */

        seedCurrentLatest();


        /*
         * Once Discord is ready, remove the old persistent
         * latest-releases leaderboard/panel.
         */

        bot.on_ready(
            [](
                const dpp::ready_t&
                )
            {
                if (
                    !dpp::run_once<
                    struct mx_remove_old_latest_panel
                    >()
                    )
                {
                    return;
                }


                removeOldLivePanel();


                std::cout
                    << "[LatestRelease] Individual release embeds enabled."
                    << std::endl;
            }
        );
    }


    /*
     * ============================================================
     * REFRESH
     * ============================================================
     *
     * UploadService calls this immediately after an upload is
     * approved.
     *
     * AddScriptCommand calls this immediately after /addscript
     * finishes.
     *
     * Instead of editing a single dashboard, we now post ONE
     * brand-new embed for each newly added script.
     */

    void refresh()
    {
        std::lock_guard<std::mutex>
            lock(
                g_refreshMutex
            );


        if (
            g_database ==
            nullptr ||
            g_bot ==
            nullptr
            )
        {
            return;
        }


        SearchService searchService(
            *g_database
        );


        /*
         * Grab enough latest configs to safely handle multiple
         * approvals happening close together.
         */

        std::vector<SearchResult> configs =
            searchService.latest(
                100
            );


        if (
            configs.empty()
            )
        {
            return;
        }


        /*
         * SearchService returns newest -> oldest.
         *
         * Flip it so if multiple configs were added, Discord gets
         * the embeds in the correct oldest -> newest order.
         */

        std::sort(
            configs.begin(),
            configs.end(),

            [](
                const SearchResult& left,
                const SearchResult& right
                )
            {
                return
                    left.id <
                    right.id;
            }
        );


        long long newestProcessed =
            g_lastSeenScriptId;


        for (
            const SearchResult& config :
            configs
            )
        {
            /*
             * Already existed before this refresh.
             */

            if (
                config.id <=
                g_lastSeenScriptId
                )
            {
                continue;
            }


            postRelease(
                config
            );


            if (
                config.id >
                newestProcessed
                )
            {
                newestProcessed =
                    config.id;
            }
        }


        g_lastSeenScriptId =
            newestProcessed;
    }
}