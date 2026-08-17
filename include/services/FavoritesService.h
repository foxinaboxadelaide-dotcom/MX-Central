#pragma once

#include "services/SearchService.h"

#include <dpp/dpp.h>

#include <cstdint>
#include <string>
#include <vector>

struct FavoriteToggleResult
{
    bool success = false;
    bool isFavorite = false;

    int favoriteCount = 0;

    std::string publicId;
    std::string scriptName;
    std::string error;
};

class FavoritesService
{
public:
    explicit FavoritesService(
        SearchService& searchService
    );

    void registerHandlers(
        dpp::cluster& bot
    );

    bool isFavorite(
        const std::string& publicId,
        std::uint64_t userId
    );

    FavoriteToggleResult toggleFavorite(
        const std::string& publicId,
        std::uint64_t userId,
        const std::string& username
    );

    std::vector<SearchResult> getFavorites(
        std::uint64_t userId,
        int limit = 25
    );

private:
    bool ensureSchema();

    bool ensureUser(
        std::uint64_t userId,
        const std::string& username
    );

    void handleFavoritesCommand(
        const dpp::slashcommand_t& event
    );

private:
    SearchService& m_searchService;
    Database& m_database;

    bool m_handlersRegistered = false;
};