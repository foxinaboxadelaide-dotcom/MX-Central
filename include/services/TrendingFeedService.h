#pragma once

#include "services/SearchService.h"

#include <dpp/dpp.h>

#include <cstdint>
#include <string>
#include <vector>

class Database;

class TrendingFeedService
{
public:
    TrendingFeedService(
        Database& database,
        dpp::cluster& bot,
        SearchService& searchService
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

    dpp::embed buildEmbed(
        const std::vector<SearchResult>& configs
    );

private:
    Database& m_database;
    dpp::cluster& m_bot;
    SearchService& m_searchService;

    std::uint64_t m_messageId = 0;

    bool m_initialized = false;
};