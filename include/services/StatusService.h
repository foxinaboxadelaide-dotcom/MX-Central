#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <cstdint>
#include <string>

class Database;

class StatusService
{
public:
    StatusService(
        Database& database,
        dpp::cluster& bot
    );

    void initialize();

    void refresh();

private:
    bool ensureTables();

    std::uint64_t loadStoredMessageId();

    void saveStoredMessageId(
        std::uint64_t messageId
    );

    void clearStoredMessageId();

    void createMessage();

    void updateHeartbeat();

    dpp::embed buildEmbed();

    std::string formatUptime() const;

private:
    Database& m_database;
    dpp::cluster& m_bot;

    std::uint64_t m_messageId = 0;

    bool m_initialized = false;
    bool m_creatingMessage = false;

    std::chrono::steady_clock::time_point
        m_startedAt;
};