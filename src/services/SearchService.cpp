#include "services/SearchService.h"

#include "database/Database.h"

#include <sqlite3.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::string columnText(
        sqlite3_stmt* statement,
        int column
    )
    {
        const unsigned char* value =
            sqlite3_column_text(
                statement,
                column
            );

        if (value == nullptr)
        {
            return "";
        }

        return reinterpret_cast<const char*>(
            value
            );
    }


    std::uint64_t columnSnowflake(
        sqlite3_stmt* statement,
        int column
    )
    {
        const std::string value =
            columnText(
                statement,
                column
            );

        if (value.empty())
        {
            return 0;
        }

        try
        {
            return std::stoull(value);
        }
        catch (...)
        {
            return 0;
        }
    }


    SearchResult readSearchResult(
        sqlite3_stmt* statement
    )
    {
        SearchResult result;

        result.id =
            sqlite3_column_int64(
                statement,
                0
            );

        result.publicId =
            columnText(
                statement,
                1
            );

        result.scriptName =
            columnText(
                statement,
                2
            );

        result.game =
            columnText(
                statement,
                3
            );

        result.platform =
            columnText(
                statement,
                4
            );

        result.sensitivity =
            columnText(
                statement,
                5
            );

        result.creatorId =
            columnSnowflake(
                statement,
                6
            );

        result.creatorName =
            columnText(
                statement,
                7
            );

        result.downloadCount =
            sqlite3_column_int(
                statement,
                8
            );

        result.averageRating =
            sqlite3_column_double(
                statement,
                9
            );

        result.ratingCount =
            sqlite3_column_int(
                statement,
                10
            );

        return result;
    }
}


/*
 * ================================================================
 * CONSTRUCTOR
 * ================================================================
 */

SearchService::SearchService(
    Database& database
)
    : m_database(database)
{
}


/*
 * ================================================================
 * SEARCH
 * ================================================================
 */

std::vector<SearchResult> SearchService::search(
    const std::string& searchText,
    int limit
)
{
    std::vector<SearchResult> results;

    if (searchText.empty())
    {
        return results;
    }


    if (limit < 1)
    {
        limit = 1;
    }


    if (limit > 25)
    {
        limit = 25;
    }


    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return results;
    }


    constexpr const char* sql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "s.creator_id, "
        "COALESCE(u.username, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0) "
        "FROM scripts s "
        "LEFT JOIN users u "
        "ON u.discord_id = CAST(s.creator_id AS TEXT) "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id "
        "WHERE s.visibility = 'public' "
        "AND ("
        "LOWER(s.public_id) LIKE LOWER(?) "
        "OR LOWER(s.script_name) LIKE LOWER(?) "
        "OR LOWER(s.game) LIKE LOWER(?)"
        ") "
        "ORDER BY "
        "CASE "
        "WHEN LOWER(s.public_id) = LOWER(?) THEN 0 "
        "WHEN LOWER(s.script_name) = LOWER(?) THEN 1 "
        "WHEN LOWER(s.script_name) LIKE LOWER(?) THEN 2 "
        "WHEN LOWER(s.script_name) LIKE LOWER(?) THEN 3 "
        "WHEN LOWER(s.game) = LOWER(?) THEN 4 "
        "WHEN LOWER(s.game) LIKE LOWER(?) THEN 5 "
        "WHEN LOWER(s.game) LIKE LOWER(?) THEN 6 "
        "ELSE 7 "
        "END ASC, "
        "s.download_count DESC, "
        "s.created_at DESC "
        "LIMIT ?;";


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
        std::cerr
            << "[SearchService] Search prepare failed: "
            << sqlite3_errmsg(database)
            << std::endl;

        return results;
    }


    const std::string contains =
        "%" + searchText + "%";

    const std::string begins =
        searchText + "%";


    /*
     * WHERE values
     */

    sqlite3_bind_text(
        statement,
        1,
        contains.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        contains.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        contains.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    /*
     * Ranking values
     */

    sqlite3_bind_text(
        statement,
        4,
        searchText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        5,
        searchText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        6,
        begins.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        7,
        contains.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        8,
        searchText.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        9,
        begins.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        10,
        contains.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        statement,
        11,
        limit
    );


    while (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        results.push_back(
            readSearchResult(
                statement
            )
        );
    }


    sqlite3_finalize(
        statement
    );


    /*
     * Search statistics.
     */

    m_database.execute(
        "UPDATE statistics "
        "SET value = value + 1, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE key = 'total_searches';"
    );


    m_database.execute(
        "INSERT INTO daily_statistics "
        "(date, searches) "
        "VALUES (date('now'), 1) "
        "ON CONFLICT(date) DO UPDATE SET "
        "searches = searches + 1;"
    );


    return results;
}


/*
 * ================================================================
 * LATEST CONFIGS
 * ================================================================
 */

std::vector<SearchResult> SearchService::latest(
    int limit
)
{
    std::vector<SearchResult> results;


    if (limit < 1)
    {
        limit = 1;
    }


    if (limit > 25)
    {
        limit = 25;
    }


    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return results;
    }


    constexpr const char* sql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "s.creator_id, "
        "COALESCE(u.username, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0) "
        "FROM scripts s "
        "LEFT JOIN users u "
        "ON u.discord_id = CAST(s.creator_id AS TEXT) "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id "
        "WHERE s.visibility = 'public' "
        "ORDER BY "
        "s.created_at DESC, "
        "s.id DESC "
        "LIMIT ?;";


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
        std::cerr
            << "[SearchService] Latest prepare failed: "
            << sqlite3_errmsg(database)
            << std::endl;

        return results;
    }


    sqlite3_bind_int(
        statement,
        1,
        limit
    );


    while (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        results.push_back(
            readSearchResult(
                statement
            )
        );
    }


    sqlite3_finalize(
        statement
    );


    return results;
}


/*
 * ================================================================
 * TRENDING CONFIGS
 * ================================================================
 */

std::vector<SearchResult> SearchService::trending(
    int limit
)
{
    std::vector<SearchResult> results;


    if (limit < 1)
    {
        limit = 1;
    }


    if (limit > 25)
    {
        limit = 25;
    }


    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        return results;
    }


    /*
     * Trending uses:
     *
     * downloads
     * +
     * average rating boost
     * +
     * amount of ratings boost
     */

    constexpr const char* sql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "s.creator_id, "
        "COALESCE(u.username, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0) "
        "FROM scripts s "
        "LEFT JOIN users u "
        "ON u.discord_id = CAST(s.creator_id AS TEXT) "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id "
        "WHERE s.visibility = 'public' "
        "ORDER BY "
        "("
        "s.download_count "
        "+ (COALESCE(r.average_rating, 0) * 5.0) "
        "+ (COALESCE(r.rating_count, 0) * 2.0)"
        ") DESC, "
        "s.download_count DESC, "
        "COALESCE(r.average_rating, 0) DESC, "
        "s.created_at DESC "
        "LIMIT ?;";


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
        std::cerr
            << "[SearchService] Trending prepare failed: "
            << sqlite3_errmsg(database)
            << std::endl;

        return results;
    }


    sqlite3_bind_int(
        statement,
        1,
        limit
    );


    while (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        results.push_back(
            readSearchResult(
                statement
            )
        );
    }


    sqlite3_finalize(
        statement
    );


    return results;
}


/*
 * ================================================================
 * LOAD FULL CONFIG
 * ================================================================
 */

ScriptPartResult SearchService::getScript(
    const std::string& publicId
)
{
    ScriptPartResult result;


    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        result.error =
            "MX database is currently unavailable.";

        return result;
    }


    constexpr const char* scriptSql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "s.creator_id, "
        "COALESCE(u.username, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0) "
        "FROM scripts s "
        "LEFT JOIN users u "
        "ON u.discord_id = CAST(s.creator_id AS TEXT) "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id "
        "WHERE s.public_id = ? "
        "AND s.visibility = 'public' "
        "LIMIT 1;";


    sqlite3_stmt* statement = nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            scriptSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "MX couldn't load this config.";

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
        sqlite3_step(statement)
        != SQLITE_ROW
        )
    {
        sqlite3_finalize(
            statement
        );

        result.error =
            "This config could not be found.";

        return result;
    }


    result.script =
        readSearchResult(
            statement
        );


    sqlite3_finalize(
        statement
    );


    /*
     * Load exact original parts.
     */

    constexpr const char* partsSql =
        "SELECT config_data "
        "FROM script_parts "
        "WHERE script_id = ? "
        "ORDER BY part_number ASC;";


    statement = nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            partsSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "MX couldn't load the config parts.";

        return result;
    }


    sqlite3_bind_int64(
        statement,
        1,
        result.script.id
    );


    while (
        sqlite3_step(statement)
        == SQLITE_ROW
        )
    {
        result.parts.push_back(
            columnText(
                statement,
                0
            )
        );
    }


    sqlite3_finalize(
        statement
    );


    if (
        result.parts.empty()
        )
    {
        result.error =
            "This config has no stored parts.";

        return result;
    }


    result.success = true;


    return result;
}


/*
 * ================================================================
 * RECORD DOWNLOAD
 * ================================================================
 */

bool SearchService::recordDownload(
    const std::string& publicId,
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


    if (
        !ensureUser(
            userId,
            username
        )
        )
    {
        return false;
    }


    /*
     * Find internal script ID.
     */

    constexpr const char* findSql =
        "SELECT id "
        "FROM scripts "
        "WHERE public_id = ? "
        "AND visibility = 'public' "
        "LIMIT 1;";


    sqlite3_stmt* statement = nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            findSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }


    sqlite3_bind_text(
        statement,
        1,
        publicId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );


    if (
        sqlite3_step(statement)
        != SQLITE_ROW
        )
    {
        sqlite3_finalize(
            statement
        );

        return false;
    }


    const long long scriptId =
        sqlite3_column_int64(
            statement,
            0
        );


    sqlite3_finalize(
        statement
    );


    /*
     * Insert download history.
     */

    constexpr const char* insertSql =
        "INSERT INTO downloads "
        "(script_id, user_id) "
        "VALUES (?, ?);";


    statement = nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            insertSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }


    const std::string discordId =
        std::to_string(
            userId
        );


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


    const bool inserted =
        sqlite3_step(statement)
        == SQLITE_DONE;


    sqlite3_finalize(
        statement
    );


    if (!inserted)
    {
        return false;
    }


    /*
     * Increase config download count.
     */

    constexpr const char* updateSql =
        "UPDATE scripts "
        "SET download_count = download_count + 1 "
        "WHERE id = ?;";


    statement = nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            updateSql,
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


        sqlite3_step(
            statement
        );


        sqlite3_finalize(
            statement
        );
    }


    /*
     * Global downloads.
     */

    m_database.execute(
        "UPDATE statistics "
        "SET value = value + 1, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE key = 'total_downloads';"
    );


    /*
     * Downloads today.
     */

    m_database.execute(
        "INSERT INTO daily_statistics "
        "(date, downloads) "
        "VALUES (date('now'), 1) "
        "ON CONFLICT(date) DO UPDATE SET "
        "downloads = downloads + 1;"
    );


    return true;
}


/*
 * ================================================================
 * ENSURE USER
 * ================================================================
 */

bool SearchService::ensureUser(
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
        std::to_string(
            userId
        );


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
        sqlite3_step(statement)
        == SQLITE_DONE;


    sqlite3_finalize(
        statement
    );


    return success;
}