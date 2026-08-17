#pragma once

#include <dpp/dpp.h>

#include <string>

#include "database/Database.h"
#include "discord/ChannelManager.h"

class Bot
{
public:
    explicit Bot(
        const std::string& token
    );

    void start();

    dpp::cluster& client();

    ChannelManager& channels();

    Database& database();

private:
    dpp::cluster m_client;

    ChannelManager m_channels;

    Database m_database;
};