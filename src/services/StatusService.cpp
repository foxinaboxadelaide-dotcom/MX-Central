#include "services/StatusService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    constexpr int REFRESH_SECONDS =
        20;

    const std::string LIVE_MESSAGE_KEY =
        "bot_status_dashboard";

    const std::string HEARTBEAT_KEY =
        "mx_finder";


    std::string connectedText(
        bool connected
    )
    {
        return connected
            ? "🟢 `Connected`"
            : "🔴 `Not Connected`";
    }


    std::string operationalText(
        bool operational
    )
    {
        return operational
            ? "🟢 `Operational`"
            : "🔴 `Unavailable`";
    }
}


/*
 * ================================================================
 * CONSTRUCTOR
 * ================================================================
 */

StatusService::StatusService(
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

void StatusService::initialize()
{
    if (m_initialized)
    {
        return;
    }

    m_initialized = true;


    if (!ensureTables())
    {
        std::cerr
            << "[Status] Could not initialize status tables."
            << std::endl;
    }


    /*
     * First heartbeat immediately.
     */

    updateHeartbeat();


    /*
     * Try to recover our existing permanent
     * #bot-status message.
     */

    m_messageId =
        loadStoredMessageId();


    if (m_messageId != 0)
    {
        const std::uint64_t storedMessageId =
            m_messageId;


        m_bot.message_get(
            storedMessageId,
            MXChannels::Main::BOT_STATUS,

            [this, storedMessageId](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (callback.is_error())
                {
                    std::cout
                        << "[Status] Old status message missing. "
                        << "Creating a new dashboard."
                        << std::endl;


                    m_messageId = 0;

                    clearStoredMessageId();

                    createMessage();

                    return;
                }


                m_messageId =
                    storedMessageId;


                std::cout
                    << "[Status] Existing bot-status dashboard found."
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
     * Refresh every 20 seconds.
     */

    m_bot.start_timer(
        [this](
            const dpp::timer&
            )
        {
            updateHeartbeat();

            refresh();
        },

        REFRESH_SECONDS
    );


    std::cout
        << "[Status] Live status system started."
        << std::endl;
}


/*
 * ================================================================
 * REFRESH
 * ================================================================
 */

void StatusService::refresh()
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
        MXChannels::Main::BOT_STATUS,

        [this](
            const dpp::confirmation_callback_t& callback
            )
        {
            /*
             * If the permanent message was deleted,
             * recreate it automatically.
             */

            if (callback.is_error())
            {
                std::cerr
                    << "[Status] Status message missing: "
                    << callback.get_error().message
                    << std::endl;


                m_messageId = 0;

                clearStoredMessageId();

                createMessage();

                return;
            }


            dpp::message message =
                callback.get<dpp::message>();


            message.set_content("");

            message.embeds.clear();


            message.add_embed(
                buildEmbed()
            );


            m_bot.message_edit(
                message,

                [](
                    const dpp::confirmation_callback_t& editCallback
                    )
                {
                    if (editCallback.is_error())
                    {
                        std::cerr
                            << "[Status] Could not update status dashboard: "
                            << editCallback.get_error().message
                            << std::endl;
                    }
                }
            );
        }
    );
}


/*
 * ================================================================
 * CREATE PERMANENT STATUS MESSAGE
 * ================================================================
 */

void StatusService::createMessage()
{
    if (m_creatingMessage)
    {
        return;
    }


    m_creatingMessage = true;


    dpp::message message;

    message.set_channel_id(
        MXChannels::Main::BOT_STATUS
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
                    << "[Status] Could not create bot-status dashboard: "
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
                << "[Status] Permanent bot-status dashboard created."
                << std::endl;
        }
    );
}


/*
 * ================================================================
 * BUILD STATUS EMBED
 * ================================================================
 */

dpp::embed StatusService::buildEmbed()
{
    /*
     * ============================================================
     * CHECK SYSTEMS
     * ============================================================
     */

    const bool databaseConnected =
        m_database.isOpen();


    const bool mainServerConnected =
        dpp::find_guild(
            MXChannels::MAIN_SERVER_ID
        ) != nullptr;


    const bool storageServerConnected =
        dpp::find_guild(
            MXChannels::STORAGE_SERVER_ID
        ) != nullptr;


    /*
     * If this function is actively executing,
     * the main MX process itself is running.
     */

    const bool botConnected =
        true;


    const bool searchOperational =
        botConnected &&
        databaseConnected &&
        mainServerConnected;


    const bool uploadOperational =
        botConnected &&
        databaseConnected &&
        mainServerConnected &&
        storageServerConnected;


    const bool cloudOperational =
        databaseConnected &&
        storageServerConnected;


    /*
     * DPP stores REST latency in seconds.
     * Convert to milliseconds.
     */

    long long pingMilliseconds = 0;


    if (m_bot.rest_ping > 0.0)
    {
        pingMilliseconds =
            static_cast<long long>(
                std::round(
                    m_bot.rest_ping *
                    1000.0
                )
                );
    }


    const std::time_t now =
        std::time(nullptr);


    /*
     * Main overall status.
     */

    const bool everythingOperational =
        botConnected &&
        databaseConnected &&
        mainServerConnected &&
        storageServerConnected;


    dpp::embed embed;


    embed
        .set_color(
            MX_PURPLE
        );


    if (everythingOperational)
    {
        embed.set_title(
            "🟢 MX FINDER — Operational"
        );
    }
    else
    {
        embed.set_title(
            "🔴 MX FINDER — Service Issue"
        );
    }


    embed.set_description(
        "Live connection status for MX Central.\n"
        "Automatically checked every **20 seconds**.\n\n"
        "Last heartbeat: <t:" +
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
        "🤖 Bot Connection",
        connectedText(
            botConnected
        ),
        true
    );


    embed.add_field(
        "🌐 Main Server",
        connectedText(
            mainServerConnected
        ),
        true
    );


    embed.add_field(
        "🗄️ Storage Server",
        connectedText(
            storageServerConnected
        ),
        true
    );


    /*
     * ROW 2
     */

    embed.add_field(
        "💾 Database",
        connectedText(
            databaseConnected
        ),
        true
    );


    embed.add_field(
        "🔎 Config Search",
        operationalText(
            searchOperational
        ),
        true
    );


    embed.add_field(
        "📤 Upload System",
        operationalText(
            uploadOperational
        ),
        true
    );


    /*
     * ROW 3
     */

    embed.add_field(
        "☁️ Cloud Storage",
        operationalText(
            cloudOperational
        ),
        true
    );


    if (pingMilliseconds > 0)
    {
        embed.add_field(
            "📡 Discord API",
            "🟢 `Connected`\n`" +
            std::to_string(
                pingMilliseconds
            ) +
            " ms`",
            true
        );
    }
    else
    {
        embed.add_field(
            "📡 Discord API",
            "🟡 `Checking...`",
            true
        );
    }


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
            "MX Central • System Status • Live"
        )
    );


    return embed;
}


/*
 * ================================================================
 * HEARTBEAT
 * ================================================================
 */

void StatusService::updateHeartbeat()
{
    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return;
    }


    constexpr const char* sql =
        "INSERT INTO service_heartbeats "
        "("
        "service_key, "
        "status, "
        "last_seen"
        ") "
        "VALUES (?, 'connected', strftime('%s','now')) "
        "ON CONFLICT(service_key) DO UPDATE SET "
        "status = 'connected', "
        "last_seen = strftime('%s','now');";


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
        HEARTBEAT_KEY.c_str(),
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
 * UPTIME
 * ================================================================
 */

std::string StatusService::formatUptime() const
{
    const auto now =
        std::chrono::steady_clock::now();


    long long seconds =
        std::chrono::duration_cast<
        std::chrono::seconds
        >(
            now -
            m_startedAt
        ).count();


    const long long days =
        seconds / 86400;


    seconds %=
        86400;


    const long long hours =
        seconds / 3600;


    seconds %=
        3600;


    const long long minutes =
        seconds / 60;


    seconds %=
        60;


    std::ostringstream output;


    if (days > 0)
    {
        output
            << days
            << "d ";
    }


    if (
        hours > 0 ||
        days > 0
        )
    {
        output
            << hours
            << "h ";
    }


    if (
        minutes > 0 ||
        hours > 0 ||
        days > 0
        )
    {
        output
            << minutes
            << "m";
    }
    else
    {
        output
            << seconds
            << "s";
    }


    return output.str();
}


/*
 * ================================================================
 * DATABASE TABLES
 * ================================================================
 */

bool StatusService::ensureTables()
{
    const bool liveMessagesReady =
        m_database.execute(
            "CREATE TABLE IF NOT EXISTS live_messages ("
            "message_key TEXT PRIMARY KEY, "
            "channel_id TEXT NOT NULL, "
            "message_id TEXT NOT NULL, "
            "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");"
        );


    const bool heartbeatReady =
        m_database.execute(
            "CREATE TABLE IF NOT EXISTS service_heartbeats ("
            "service_key TEXT PRIMARY KEY, "
            "status TEXT NOT NULL, "
            "last_seen INTEGER NOT NULL"
            ");"
        );


    return
        liveMessagesReady &&
        heartbeatReady;
}


/*
 * ================================================================
 * LOAD MESSAGE ID
 * ================================================================
 */

std::uint64_t StatusService::loadStoredMessageId()
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

void StatusService::saveStoredMessageId(
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
        ) != SQLITE_OK
        )
    {
        return;
    }


    const std::string channelId =
        std::to_string(
            MXChannels::Main::BOT_STATUS
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

void StatusService::clearStoredMessageId()
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