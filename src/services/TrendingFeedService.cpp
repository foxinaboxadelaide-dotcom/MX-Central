#include "services/TrendingFeedService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include <sqlite3.h>

#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    constexpr int REFRESH_SECONDS =
        30;

    const std::string LIVE_MESSAGE_KEY =
        "trending_feed";


    std::string ratingText(
        double average,
        int ratingCount
    )
    {
        if (ratingCount <= 0)
        {
            return "Not rated yet";
        }

        std::ostringstream stream;

        stream
            << std::fixed
            << std::setprecision(1)
            << average
            << "/5";

        return stream.str();
    }


    std::string rankingIcon(
        std::size_t index
    )
    {
        if (index == 0)
        {
            return "🥇";
        }

        if (index == 1)
        {
            return "🥈";
        }

        if (index == 2)
        {
            return "🥉";
        }

        return "🔥";
    }
}


TrendingFeedService::TrendingFeedService(
    Database& database,
    dpp::cluster& bot,
    SearchService& searchService
)
    : m_database(database),
    m_bot(bot),
    m_searchService(searchService)
{
}


/*
 * ================================================================
 * INITIALIZE
 * ================================================================
 */

void TrendingFeedService::initialize()
{
    if (m_initialized)
    {
        return;
    }

    m_initialized = true;


    /*
     * Small state table used to remember the Discord
     * message ID across bot restarts.
     */

    if (!ensureStateTable())
    {
        std::cerr
            << "[TrendingFeed] Could not create "
            << "live message state table."
            << std::endl;
    }


    m_messageId =
        loadStoredMessageId();


    /*
     * If we already know the message ID from a previous
     * bot session, verify that the message still exists.
     */

    if (m_messageId != 0)
    {
        const std::uint64_t existingId =
            m_messageId;


        m_bot.message_get(
            existingId,
            MXChannels::Main::TRENDING_CONFIGS,

            [this, existingId](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (callback.is_error())
                {
                    std::cout
                        << "[TrendingFeed] Stored message no longer exists. "
                        << "Creating a new leaderboard."
                        << std::endl;

                    m_messageId = 0;

                    clearStoredMessageId();

                    createMessage();

                    return;
                }


                m_messageId =
                    existingId;


                std::cout
                    << "[TrendingFeed] Existing leaderboard found."
                    << std::endl;


                refresh();
            }
        );
    }
    else
    {
        createMessage();
    }


    /*
     * Keep the SAME message updated every 30 seconds.
     */

    m_bot.start_timer(
        [this](
            const dpp::timer&
            )
        {
            refresh();
        },

        REFRESH_SECONDS
    );


    std::cout
        << "[TrendingFeed] Live leaderboard started."
        << std::endl;
}


/*
 * ================================================================
 * REFRESH
 * ================================================================
 */

void TrendingFeedService::refresh()
{
    /*
     * If no permanent message exists yet,
     * create one instead.
     */

    if (m_messageId == 0)
    {
        createMessage();

        return;
    }


    const std::vector<SearchResult> configs =
        m_searchService.trending(10);


    const std::uint64_t currentMessageId =
        m_messageId;


    /*
     * Get the existing message first.
     *
     * Then modify that exact same Discord message
     * instead of sending another one.
     */

    m_bot.message_get(
        currentMessageId,
        MXChannels::Main::TRENDING_CONFIGS,

        [this, configs](
            const dpp::confirmation_callback_t& callback
            )
        {
            if (callback.is_error())
            {
                std::cerr
                    << "[TrendingFeed] Could not find leaderboard message: "
                    << callback.get_error().message
                    << std::endl;


                m_messageId = 0;

                clearStoredMessageId();

                createMessage();

                return;
            }


            dpp::message message =
                callback.get<dpp::message>();


            /*
             * Remove old content/embed.
             */

            message.set_content("");

            message.embeds.clear();


            /*
             * Add fresh leaderboard.
             */

            message.add_embed(
                buildEmbed(
                    configs
                )
            );


            /*
             * Edit the existing message.
             */

            m_bot.message_edit(
                message,

                [this](
                    const dpp::confirmation_callback_t& editCallback
                    )
                {
                    if (editCallback.is_error())
                    {
                        std::cerr
                            << "[TrendingFeed] Failed to update leaderboard: "
                            << editCallback.get_error().message
                            << std::endl;

                        return;
                    }


                    std::cout
                        << "[TrendingFeed] Leaderboard refreshed."
                        << std::endl;
                }
            );
        }
    );
}


/*
 * ================================================================
 * CREATE PERMANENT MESSAGE
 * ================================================================
 */

void TrendingFeedService::createMessage()
{
    const std::vector<SearchResult> configs =
        m_searchService.trending(10);


    dpp::message message;

    message.set_channel_id(
        MXChannels::Main::TRENDING_CONFIGS
    );


    message.add_embed(
        buildEmbed(
            configs
        )
    );


    m_bot.message_create(
        message,

        [this](
            const dpp::confirmation_callback_t& callback
            )
        {
            if (callback.is_error())
            {
                std::cerr
                    << "[TrendingFeed] Could not create leaderboard: "
                    << callback.get_error().message
                    << std::endl;

                return;
            }


            const dpp::message sentMessage =
                callback.get<dpp::message>();


            m_messageId =
                static_cast<std::uint64_t>(
                    sentMessage.id
                    );


            saveStoredMessageId(
                m_messageId
            );


            std::cout
                << "[TrendingFeed] Created permanent leaderboard message."
                << std::endl;
        }
    );
}


/*
 * ================================================================
 * BUILD LEADERBOARD EMBED
 * ================================================================
 */

dpp::embed TrendingFeedService::buildEmbed(
    const std::vector<SearchResult>& configs
)
{
    dpp::embed embed;


    embed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "🔥 Trending MX Configs"
        );


    /*
     * No configs yet.
     */

    if (configs.empty())
    {
        embed
            .set_description(
                "There are currently no approved configs "
                "in the MX library.\n\n"
                "Trending rankings will appear here automatically."
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Live Trending"
                )
            );


        return embed;
    }


    /*
     * Header.
     */

    const std::time_t now =
        std::time(nullptr);


    embed.set_description(
        "The most popular configs across MX Central.\n"
        "Rankings are based on **downloads and community ratings**.\n\n"
        "Last updated: <t:" +
        std::to_string(
            static_cast<long long>(
                now
                )
        ) +
        ":R>"
    );


    /*
     * Top 10.
     */

    for (
        std::size_t index = 0;
        index < configs.size();
        ++index
        )
    {
        const SearchResult& config =
            configs[index];


        const std::string title =
            rankingIcon(index) +
            " #" +
            std::to_string(
                index + 1
            ) +
            " • " +
            config.scriptName;


        std::string value =
            "**Game:** `" +
            config.game +
            "`\n"
            "**Platform:** `" +
            config.platform +
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
            "`";


        if (config.ratingCount > 0)
        {
            value +=
                " • `" +
                std::to_string(
                    config.ratingCount
                ) +
                " rating";

            if (
                config.ratingCount != 1
                )
            {
                value += "s";
            }

            value += "`";
        }


        value +=
            "\n**MX ID:** `" +
            config.publicId +
            "`";


        embed.add_field(
            title,
            value,
            false
        );
    }


    embed.set_footer(
        dpp::embed_footer()
        .set_text(
            "MX Central • Live Trending • Auto Updates"
        )
    );


    return embed;
}


/*
 * ================================================================
 * DATABASE STATE TABLE
 * ================================================================
 */

bool TrendingFeedService::ensureStateTable()
{
    return m_database.execute(
        "CREATE TABLE IF NOT EXISTS live_messages ("
        "message_key TEXT PRIMARY KEY, "
        "channel_id TEXT NOT NULL, "
        "message_id TEXT NOT NULL, "
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
    );
}


/*
 * ================================================================
 * LOAD MESSAGE ID
 * ================================================================
 */

std::uint64_t TrendingFeedService::loadStoredMessageId()
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
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
        ) != SQLITE_OK
        )
    {
        return 0;
    }


    sqlite3_bind_text(
        statement,
        1,
        LIVE_MESSAGE_KEY.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    std::uint64_t messageId =
        0;


    if (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        const unsigned char* value =
            sqlite3_column_text(
                statement,
                0
            );


        if (value != nullptr)
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
                messageId = 0;
            }
        }
    }


    sqlite3_finalize(
        statement
    );


    return messageId;
}


/*
 * ================================================================
 * SAVE MESSAGE ID
 * ================================================================
 */

void TrendingFeedService::saveStoredMessageId(
    std::uint64_t messageId
)
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return;
    }


    constexpr const char* sql =
        "INSERT INTO live_messages "
        "("
        "message_key, "
        "channel_id, "
        "message_id"
        ") "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(message_key) "
        "DO UPDATE SET "
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
        ) != SQLITE_OK
        )
    {
        return;
    }


    const std::string channelId =
        std::to_string(
            MXChannels::Main::TRENDING_CONFIGS
        );


    const std::string messageIdText =
        std::to_string(
            messageId
        );


    sqlite3_bind_text(
        statement,
        1,
        LIVE_MESSAGE_KEY.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    sqlite3_bind_text(
        statement,
        2,
        channelId.c_str(),
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
 * ================================================================
 * CLEAR MESSAGE ID
 * ================================================================
 */

void TrendingFeedService::clearStoredMessageId()
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return;
    }


    constexpr const char* sql =
        "DELETE FROM live_messages "
        "WHERE message_key = ?;";


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
        return;
    }


    sqlite3_bind_text(
        statement,
        1,
        LIVE_MESSAGE_KEY.c_str(),
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