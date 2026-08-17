#include "services/StatisticsService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    constexpr int REFRESH_SECONDS =
        30;

    const std::string LIVE_MESSAGE_KEY =
        "statistics_dashboard";


    std::string formatRating(
        double rating
    )
    {
        if (rating <= 0.0)
        {
            return "Not rated";
        }

        std::ostringstream stream;

        stream
            << std::fixed
            << std::setprecision(1)
            << rating
            << "/5";

        return stream.str();
    }
}


StatisticsService::StatisticsService(
    Database& database,
    dpp::cluster& bot
)
    : m_database(database),
    m_bot(bot),
    m_startedAt(
        std::chrono::steady_clock::now()
    )
{
}


/*
 * ================================================================
 * INITIALIZE
 * ================================================================
 */

void StatisticsService::initialize()
{
    if (m_initialized)
    {
        return;
    }

    m_initialized = true;


    /*
     * The trending feed also uses this table.
     *
     * CREATE TABLE IF NOT EXISTS makes this safe
     * regardless of which service starts first.
     */

    if (!ensureStateTable())
    {
        std::cerr
            << "[Statistics] Could not create "
            << "live message state table."
            << std::endl;
    }


    m_messageId =
        loadStoredMessageId();


    /*
     * If MX remembers an old statistics message,
     * check that it still exists.
     */

    if (m_messageId != 0)
    {
        const std::uint64_t storedMessageId =
            m_messageId;


        m_bot.message_get(
            storedMessageId,
            MXChannels::Main::STATISTICS,

            [this, storedMessageId](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (callback.is_error())
                {
                    std::cout
                        << "[Statistics] Old dashboard message "
                        << "was not found. Creating a new one."
                        << std::endl;


                    m_messageId = 0;

                    clearStoredMessageId();

                    createMessage();

                    return;
                }


                m_messageId =
                    storedMessageId;


                std::cout
                    << "[Statistics] Existing statistics "
                    << "dashboard found."
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
     * Refresh the dashboard every 30 seconds.
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
        << "[Statistics] Live dashboard started."
        << std::endl;
}


/*
 * ================================================================
 * REFRESH
 * ================================================================
 */

void StatisticsService::refresh()
{
    if (m_messageId == 0)
    {
        createMessage();

        return;
    }


    const std::uint64_t currentMessageId =
        m_messageId;


    m_bot.message_get(
        currentMessageId,
        MXChannels::Main::STATISTICS,

        [this](
            const dpp::confirmation_callback_t& callback
            )
        {
            /*
             * Someone deleted the dashboard.
             *
             * MX automatically recreates it.
             */

            if (callback.is_error())
            {
                std::cerr
                    << "[Statistics] Dashboard message "
                    << "could not be found: "
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
             * Remove the old embed.
             */

            message.set_content("");

            message.embeds.clear();


            /*
             * Add freshly calculated statistics.
             */

            message.add_embed(
                buildEmbed()
            );


            /*
             * Edit the SAME Discord message.
             */

            m_bot.message_edit(
                message,

                [](
                    const dpp::confirmation_callback_t& editCallback
                    )
                {
                    if (editCallback.is_error())
                    {
                        std::cerr
                            << "[Statistics] Dashboard update failed: "
                            << editCallback.get_error().message
                            << std::endl;

                        return;
                    }
                }
            );
        }
    );
}


/*
 * ================================================================
 * CREATE DASHBOARD
 * ================================================================
 */

void StatisticsService::createMessage()
{
    /*
     * Prevent two timers/callbacks accidentally
     * creating duplicate dashboards simultaneously.
     */

    if (m_creatingMessage)
    {
        return;
    }


    m_creatingMessage = true;


    dpp::message message;

    message.set_channel_id(
        MXChannels::Main::STATISTICS
    );


    message.add_embed(
        buildEmbed()
    );


    m_bot.message_create(
        message,

        [this](
            const dpp::confirmation_callback_t& callback
            )
        {
            m_creatingMessage = false;


            if (callback.is_error())
            {
                std::cerr
                    << "[Statistics] Could not create dashboard: "
                    << callback.get_error().message
                    << std::endl;

                return;
            }


            const dpp::message createdMessage =
                callback.get<dpp::message>();


            m_messageId =
                static_cast<std::uint64_t>(
                    createdMessage.id
                    );


            saveStoredMessageId(
                m_messageId
            );


            std::cout
                << "[Statistics] Permanent statistics "
                << "dashboard created."
                << std::endl;
        }
    );
}


/*
 * ================================================================
 * BUILD EMBED
 * ================================================================
 */

dpp::embed StatisticsService::buildEmbed()
{
    /*
     * ============================================================
     * DISCORD DATA
     * ============================================================
     */

    const std::uint64_t members =
        getMemberCount();


    const bool mainServerOnline =
        dpp::find_guild(
            MXChannels::MAIN_SERVER_ID
        ) != nullptr;


    const bool storageServerOnline =
        dpp::find_guild(
            MXChannels::STORAGE_SERVER_ID
        ) != nullptr;


    const bool databaseOnline =
        m_database.isOpen();


    /*
     * ============================================================
     * DATABASE DATA
     * ============================================================
     */

    const long long totalConfigs =
        queryInteger(
            "SELECT COUNT(*) "
            "FROM scripts "
            "WHERE visibility = 'public';"
        );


    const long long downloadsToday =
        queryInteger(
            "SELECT COALESCE(downloads, 0) "
            "FROM daily_statistics "
            "WHERE date = date('now') "
            "LIMIT 1;"
        );


    const long long uploadsToday =
        queryInteger(
            "SELECT COALESCE(uploads, 0) "
            "FROM daily_statistics "
            "WHERE date = date('now') "
            "LIMIT 1;"
        );


    const long long searchesToday =
        queryInteger(
            "SELECT COALESCE(searches, 0) "
            "FROM daily_statistics "
            "WHERE date = date('now') "
            "LIMIT 1;"
        );


    /*
     * Weighted community-wide rating.
     *
     * Every individual rating contributes equally.
     */

    const double averageRating =
        queryDouble(
            "SELECT COALESCE(AVG(rating), 0) "
            "FROM ratings;"
        );


    /*
     * Storage/cloud is considered operational when
     * both the SQLite database and private storage
     * Discord server are available.
     */

    const bool cloudOperational =
        databaseOnline &&
        storageServerOnline;


    /*
     * ============================================================
     * EMBED
     * ============================================================
     */

    const std::time_t now =
        std::time(nullptr);


    dpp::embed embed;


    embed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "📊 MX Central Statistics"
        )

        .set_description(
            "Live activity across the MX Central network.\n"
            "Automatically updated every **30 seconds**.\n\n"
            "Last updated: <t:" +
            std::to_string(
                static_cast<long long>(
                    now
                    )
            ) +
            ":R>"
        );


    /*
     * ROW 1
     */

    embed.add_field(
        "👥 Members",
        "`" +
        std::to_string(
            members
        ) +
        "`",
        true
    );


    embed.add_field(
        "📦 Configs",
        "`" +
        std::to_string(
            totalConfigs
        ) +
        "`",
        true
    );


    embed.add_field(
        "📥 Downloads Today",
        "`" +
        std::to_string(
            downloadsToday
        ) +
        "`",
        true
    );


    /*
     * ROW 2
     */

    embed.add_field(
        "📤 Uploads Today",
        "`" +
        std::to_string(
            uploadsToday
        ) +
        "`",
        true
    );


    embed.add_field(
        "🔎 Searches Today",
        "`" +
        std::to_string(
            searchesToday
        ) +
        "`",
        true
    );


    embed.add_field(
        "⭐ Average Rating",
        "`" +
        formatRating(
            averageRating
        ) +
        "`",
        true
    );


    /*
     * ROW 3
     */

    embed.add_field(
        "🤖 Bot Status",

        mainServerOnline
        ? "🟢 `Online`"
        : "🔴 `Offline`",

        true
    );


    embed.add_field(
        "☁️ Cloud Status",

        cloudOperational
        ? "🟢 `Operational`"
        : "🔴 `Unavailable`",

        true
    );


    embed.add_field(
        "⏱️ Uptime",
        "`" +
        formatUptime() +
        "`",
        true
    );


    embed.set_footer(
        dpp::embed_footer()
        .set_text(
            "MX Central • Live Statistics • Auto Updates"
        )
    );


    return embed;
}


/*
 * ================================================================
 * MEMBER COUNT
 * ================================================================
 */

std::uint64_t StatisticsService::getMemberCount()
{
    dpp::guild* guild =
        dpp::find_guild(
            MXChannels::MAIN_SERVER_ID
        );


    if (guild == nullptr)
    {
        return 0;
    }


    /*
     * Discord normally provides member_count.
     */

    if (guild->member_count > 0)
    {
        return static_cast<std::uint64_t>(
            guild->member_count
            );
    }


    /*
     * Fallback if Discord supplied member_count as 0.
     */

    return static_cast<std::uint64_t>(
        guild->members.size()
        );
}


/*
 * ================================================================
 * QUERY INTEGER
 * ================================================================
 */

long long StatisticsService::queryInteger(
    const std::string& sql
)
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return 0;
    }


    sqlite3_stmt* statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            sql.c_str(),
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        std::cerr
            << "[Statistics] Integer query failed: "
            << sqlite3_errmsg(database)
            << std::endl;

        return 0;
    }


    long long result =
        0;


    if (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        result =
            sqlite3_column_int64(
                statement,
                0
            );
    }


    sqlite3_finalize(
        statement
    );


    return result;
}


/*
 * ================================================================
 * QUERY DOUBLE
 * ================================================================
 */

double StatisticsService::queryDouble(
    const std::string& sql
)
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return 0.0;
    }


    sqlite3_stmt* statement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            sql.c_str(),
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        std::cerr
            << "[Statistics] Double query failed: "
            << sqlite3_errmsg(database)
            << std::endl;

        return 0.0;
    }


    double result =
        0.0;


    if (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        result =
            sqlite3_column_double(
                statement,
                0
            );
    }


    sqlite3_finalize(
        statement
    );


    return result;
}


/*
 * ================================================================
 * UPTIME
 * ================================================================
 */

std::string StatisticsService::formatUptime() const
{
    const auto now =
        std::chrono::steady_clock::now();


    long long totalSeconds =
        std::chrono::duration_cast<
        std::chrono::seconds
        >(
            now -
            m_startedAt
        ).count();


    const long long days =
        totalSeconds /
        86400;


    totalSeconds %=
        86400;


    const long long hours =
        totalSeconds /
        3600;


    totalSeconds %=
        3600;


    const long long minutes =
        totalSeconds /
        60;


    const long long seconds =
        totalSeconds %
        60;


    std::ostringstream stream;


    if (days > 0)
    {
        stream
            << days
            << "d ";
    }


    if (
        hours > 0 ||
        days > 0
        )
    {
        stream
            << hours
            << "h ";
    }


    if (
        minutes > 0 ||
        hours > 0 ||
        days > 0
        )
    {
        stream
            << minutes
            << "m";
    }
    else
    {
        stream
            << seconds
            << "s";
    }


    return stream.str();
}


/*
 * ================================================================
 * LIVE MESSAGE STATE TABLE
 * ================================================================
 */

bool StatisticsService::ensureStateTable()
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
 * LOAD STORED MESSAGE
 * ================================================================
 */

std::uint64_t StatisticsService::loadStoredMessageId()
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
 * SAVE STORED MESSAGE
 * ================================================================
 */

void StatisticsService::saveStoredMessageId(
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
            MXChannels::Main::STATISTICS
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
 * CLEAR STORED MESSAGE
 * ================================================================
 */

void StatisticsService::clearStoredMessageId()
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