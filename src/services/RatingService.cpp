#include "services/RatingService.h"

#include "database/Database.h"

#include <sqlite3.h>

#include <cstdint>
#include <string>

RatingService::RatingService(
    Database& database
)
    : m_database(database)
{
}

bool RatingService::ensureUser(
    std::uint64_t userId,
    const std::string& username
)
{
    sqlite3* database =
        m_database.handle();

    if (database == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "INSERT INTO users "
        "(discord_id, username) "
        "VALUES (?, ?) "
        "ON CONFLICT(discord_id) DO UPDATE SET "
        "username = excluded.username, "
        "last_seen_at = CURRENT_TIMESTAMP;";

    sqlite3_stmt* statement = nullptr;

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
        return false;
    }

    const std::string discordId =
        std::to_string(userId);

    sqlite3_bind_text(
        statement,
        1,
        discordId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    const bool success =
        sqlite3_step(statement) ==
        SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

RatingResult RatingService::rateConfig(
    const std::string& publicId,
    std::uint64_t userId,
    const std::string& username,
    int rating
)
{
    RatingResult result;

    result.publicId = publicId;
    result.rating = rating;

    if (
        rating < 1 ||
        rating > 5
        )
    {
        result.error =
            "Ratings must be between 1 and 5.";

        return result;
    }

    sqlite3* database =
        m_database.handle();

    if (database == nullptr)
    {
        result.error =
            "MX database is unavailable.";

        return result;
    }

    if (
        !ensureUser(
            userId,
            username
        )
        )
    {
        result.error =
            "MX couldn't create your user record.";

        return result;
    }

    /*
     * Find config.
     */

    constexpr const char* findScriptSql =
        "SELECT id, script_name "
        "FROM scripts "
        "WHERE public_id = ? "
        "AND visibility = 'public' "
        "LIMIT 1;";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            database,
            findScriptSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "MX couldn't prepare the rating.";

        return result;
    }

    sqlite3_bind_text(
        statement,
        1,
        publicId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    if (
        sqlite3_step(statement) !=
        SQLITE_ROW
        )
    {
        sqlite3_finalize(statement);

        result.error =
            "That config could not be found.";

        return result;
    }

    const long long scriptId =
        sqlite3_column_int64(
            statement,
            0
        );

    const unsigned char* scriptName =
        sqlite3_column_text(
            statement,
            1
        );

    if (scriptName != nullptr)
    {
        result.scriptName =
            reinterpret_cast<const char*>(
                scriptName
                );
    }

    sqlite3_finalize(statement);

    const std::string discordId =
        std::to_string(userId);

    /*
     * Check whether this user already rated it.
     */

    constexpr const char* existingSql =
        "SELECT rating "
        "FROM ratings "
        "WHERE script_id = ? "
        "AND user_id = ? "
        "LIMIT 1;";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            database,
            existingSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "MX couldn't check your existing rating.";

        return result;
    }

    sqlite3_bind_int64(
        statement,
        1,
        scriptId
    );

    sqlite3_bind_text(
        statement,
        2,
        discordId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    result.updatedExisting =
        sqlite3_step(statement) ==
        SQLITE_ROW;

    sqlite3_finalize(statement);

    /*
     * Insert new rating or update existing rating.
     */

    constexpr const char* ratingSql =
        "INSERT INTO ratings "
        "(script_id, user_id, rating) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(script_id, user_id) "
        "DO UPDATE SET "
        "rating = excluded.rating;";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            database,
            ratingSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "MX couldn't prepare the rating update.";

        return result;
    }

    sqlite3_bind_int64(
        statement,
        1,
        scriptId
    );

    sqlite3_bind_text(
        statement,
        2,
        discordId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        statement,
        3,
        rating
    );

    if (
        sqlite3_step(statement) !=
        SQLITE_DONE
        )
    {
        sqlite3_finalize(statement);

        result.error =
            "MX couldn't save your rating.";

        return result;
    }

    sqlite3_finalize(statement);

    /*
     * Only count first-time ratings in global/daily stats.
     *
     * Updating 4 stars -> 5 stars shouldn't pretend
     * another separate rating was created.
     */

    if (!result.updatedExisting)
    {
        m_database.execute(
            "UPDATE statistics "
            "SET value = value + 1, "
            "updated_at = CURRENT_TIMESTAMP "
            "WHERE key = 'total_ratings';"
        );

        m_database.execute(
            "INSERT INTO daily_statistics "
            "(date, ratings) "
            "VALUES (date('now'), 1) "
            "ON CONFLICT(date) DO UPDATE SET "
            "ratings = ratings + 1;"
        );
    }

    /*
     * Get updated average.
     */

    constexpr const char* summarySql =
        "SELECT "
        "average_rating, "
        "rating_count "
        "FROM script_rating_summary "
        "WHERE script_id = ? "
        "LIMIT 1;";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            database,
            summarySql,
            -1,
            &statement,
            nullptr
        ) == SQLITE_OK
        )
    {
        sqlite3_bind_int64(
            statement,
            1,
            scriptId
        );

        if (
            sqlite3_step(statement) ==
            SQLITE_ROW
            )
        {
            result.averageRating =
                sqlite3_column_double(
                    statement,
                    0
                );

            result.ratingCount =
                sqlite3_column_int(
                    statement,
                    1
                );
        }

        sqlite3_finalize(statement);
    }

    result.success = true;

    return result;
}