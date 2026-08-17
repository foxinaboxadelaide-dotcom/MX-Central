#pragma once

#include <dpp/dpp.h>

#include <chrono>
#include <cstdint>
#include <string>

class Database;

class StatisticsService
{
public:
    StatisticsService(
        Database& database,
        dpp::cluster& bot
    );

    void initialize();

    void refresh();

private:
    bool ensureStateTable();

    std::uint64_t loadStoredMessageId();

    void saveStoredMessageId(
        std::uint64_t messageId
    );

    void clearStoredMessageId();

    void createMessage();

    dpp::embed buildEmbed();

    long long queryInteger(
        const std::string& sql
    );

    double queryDouble(
        const std::string& sql
    );

    std::uint64_t getMemberCount();

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