#include "bot/Bot.h"

#include "bot/CommandRegistry.h"
#include "bot/EventHandler.h"

#include <iostream>
#include <stdexcept>
#include <string>

Bot::Bot(
    const std::string& token
)
    : m_client(
        token,

        /*
         * Message content is required because
         * MX captures config parts pasted into
         * #upload-config.
         */
        dpp::i_default_intents |
        dpp::i_message_content |
        dpp::i_guild_members
    ),
    m_channels(m_client),
    m_database()
{
    std::cout
        << "[MX] Initializing database..."
        << std::endl;

    const std::string databasePath =
        "data/database/mx_central.db";

    if (
        !m_database.open(
            databasePath
        )
        )
    {
        throw std::runtime_error(
            "MX Central failed to open the database."
        );
    }

    if (
        !m_database.initialize()
        )
    {
        throw std::runtime_error(
            "MX Central failed to initialize the database."
        );
    }

    std::cout
        << "[MX] Database ready."
        << std::endl;


    /*
     * Discord/system events.
     */
    EventHandler::registerHandlers(
        m_client,
        m_database
    );


    /*
     * Commands + interaction systems.
     */
    CommandRegistry::initialize(
        m_client,
        m_database
    );
}

void Bot::start()
{
    std::cout
        << "MX Central starting..."
        << std::endl;

    std::cout
        << "Connecting to Discord..."
        << std::endl;

    m_client.start(
        dpp::st_wait
    );
}

dpp::cluster& Bot::client()
{
    return m_client;
}

ChannelManager& Bot::channels()
{
    return m_channels;
}

Database& Bot::database()
{
    return m_database;
}