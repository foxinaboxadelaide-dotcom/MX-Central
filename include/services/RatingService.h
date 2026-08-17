#pragma once

#include <cstdint>
#include <string>

class Database;

struct RatingResult
{
    bool success = false;
    bool updatedExisting = false;

    std::string publicId;
    std::string scriptName;

    int rating = 0;
    double averageRating = 0.0;
    int ratingCount = 0;

    std::string error;
};

class RatingService
{
public:
    explicit RatingService(
        Database& database
    );

    RatingResult rateConfig(
        const std::string& publicId,
        std::uint64_t userId,
        const std::string& username,
        int rating
    );

private:
    bool ensureUser(
        std::uint64_t userId,
        const std::string& username
    );

private:
    Database& m_database;
};