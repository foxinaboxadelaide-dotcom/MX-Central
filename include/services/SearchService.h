#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Database;

struct SearchResult
{
    long long id = 0;

    std::string publicId;
    std::string scriptName;
    std::string game;
    std::string platform;
    std::string sensitivity;

    std::uint64_t creatorId = 0;
    std::string creatorName;

    int downloadCount = 0;

    double averageRating = 0.0;
    int ratingCount = 0;
};

struct ScriptPartResult
{
    bool success = false;

    SearchResult script;

    std::vector<std::string> parts;

    std::string error;
};

class SearchService
{
public:
    explicit SearchService(
        Database& database
    );

    Database& database()
    {
        return m_database;
    }

    std::vector<SearchResult> search(
        const std::string& searchText,
        int limit = 5
    );

    std::vector<SearchResult> latest(
        int limit = 10
    );

    std::vector<SearchResult> trending(
        int limit = 10
    );

    ScriptPartResult getScript(
        const std::string& publicId
    );

    bool recordDownload(
        const std::string& publicId,
        std::uint64_t userId,
        const std::string& username
    );

private:
    bool ensureUser(
        std::uint64_t userId,
        const std::string& username
    );

private:
    Database& m_database;
};