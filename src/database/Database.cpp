#include "database/Database.h"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

Database::Database()
    : m_database(nullptr)
{
}

Database::~Database()
{
    close();
}

bool Database::open(const std::string& databasePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database != nullptr)
    {
        return true;
    }

    try
    {
        const std::filesystem::path path(databasePath);

        if (path.has_parent_path())
        {
            std::filesystem::create_directories(
                path.parent_path()
            );
        }
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "[Database] Failed to create database directory: "
            << exception.what()
            << std::endl;

        return false;
    }

    const int result =
        sqlite3_open(
            databasePath.c_str(),
            &m_database
        );

    if (result != SQLITE_OK)
    {
        std::cerr
            << "[Database] Failed to open database";

        if (m_database != nullptr)
        {
            std::cerr
                << ": "
                << sqlite3_errmsg(m_database);

            sqlite3_close(m_database);
            m_database = nullptr;
        }

        std::cerr << std::endl;

        return false;
    }

    sqlite3_busy_timeout(
        m_database,
        5000
    );

    char* errorMessage = nullptr;

    /*
     * Enable foreign keys.
     */
    sqlite3_exec(
        m_database,
        "PRAGMA foreign_keys = ON;",
        nullptr,
        nullptr,
        &errorMessage
    );

    if (errorMessage != nullptr)
    {
        std::cerr
            << "[Database] Foreign key warning: "
            << errorMessage
            << std::endl;

        sqlite3_free(errorMessage);
        errorMessage = nullptr;
    }

    /*
     * WAL improves SQLite behaviour for the type of
     * read/write workload MX Central will eventually have.
     */
    sqlite3_exec(
        m_database,
        "PRAGMA journal_mode = WAL;",
        nullptr,
        nullptr,
        &errorMessage
    );

    if (errorMessage != nullptr)
    {
        std::cerr
            << "[Database] WAL warning: "
            << errorMessage
            << std::endl;

        sqlite3_free(errorMessage);
    }

    std::cout
        << "[Database] Connected: "
        << databasePath
        << std::endl;

    return true;
}

void Database::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database == nullptr)
    {
        return;
    }

    sqlite3_close(m_database);
    m_database = nullptr;

    std::cout
        << "[Database] Connection closed."
        << std::endl;
}

bool Database::initialize()
{
    if (!isOpen())
    {
        std::cerr
            << "[Database] Cannot initialize because "
            << "the database is not open."
            << std::endl;

        return false;
    }

    if (!createMigrationTable())
    {
        std::cerr
            << "[Database] Failed to create migration table."
            << std::endl;

        return false;
    }

    if (!runMigrations())
    {
        std::cerr
            << "[Database] Failed to run migrations."
            << std::endl;

        return false;
    }

    std::cout
        << "[Database] Database initialized successfully."
        << std::endl;

    return true;
}

bool Database::isOpen() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_database != nullptr;
}

bool Database::execute(const std::string& sql)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database == nullptr)
    {
        std::cerr
            << "[Database] Cannot execute SQL because "
            << "the database is not open."
            << std::endl;

        return false;
    }

    char* errorMessage = nullptr;

    const int result =
        sqlite3_exec(
            m_database,
            sql.c_str(),
            nullptr,
            nullptr,
            &errorMessage
        );

    if (result != SQLITE_OK)
    {
        std::cerr
            << "[Database] SQL error: ";

        if (errorMessage != nullptr)
        {
            std::cerr << errorMessage;
            sqlite3_free(errorMessage);
        }
        else
        {
            std::cerr << sqlite3_errmsg(m_database);
        }

        std::cerr << std::endl;

        return false;
    }

    return true;
}

bool Database::executeFile(const std::string& filePath)
{
    std::ifstream file(filePath);

    if (!file.is_open())
    {
        std::cerr
            << "[Database] Could not open SQL file: "
            << filePath
            << std::endl;

        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return execute(
        buffer.str()
    );
}

sqlite3* Database::handle()
{
    return m_database;
}

long long Database::lastInsertId() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database == nullptr)
    {
        return 0;
    }

    return static_cast<long long>(
        sqlite3_last_insert_rowid(m_database)
        );
}

bool Database::createMigrationTable()
{
    return execute(
        R"SQL(
            CREATE TABLE IF NOT EXISTS schema_migrations
            (
                version INTEGER PRIMARY KEY,
                applied_at TEXT NOT NULL
                    DEFAULT CURRENT_TIMESTAMP
            );
        )SQL"
    );
}

bool Database::runMigrations()
{
    const std::vector<std::pair<int, std::string>> migrations =
    {
        {
            1,
            "database/migrations/001_initial.sql"
        },
        {
            2,
            "database/migrations/002_scripts.sql"
        },
        {
            3,
            "database/migrations/003_ratings.sql"
        },
        {
            4,
            "database/migrations/004_statistics.sql"
        }
    };

    for (const auto& migration : migrations)
    {
        const int version =
            migration.first;

        const std::string& filePath =
            migration.second;

        if (hasMigration(version))
        {
            continue;
        }

        std::cout
            << "[Database] Applying migration "
            << version
            << ": "
            << filePath
            << std::endl;

        if (!execute("BEGIN TRANSACTION;"))
        {
            return false;
        }

        if (!executeFile(filePath))
        {
            execute("ROLLBACK;");

            std::cerr
                << "[Database] Migration "
                << version
                << " failed."
                << std::endl;

            return false;
        }

        if (!markMigrationComplete(version))
        {
            execute("ROLLBACK;");

            return false;
        }

        if (!execute("COMMIT;"))
        {
            execute("ROLLBACK;");

            return false;
        }

        std::cout
            << "[Database] Migration "
            << version
            << " completed."
            << std::endl;
    }

    return true;
}

bool Database::hasMigration(int version)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "SELECT 1 "
        "FROM schema_migrations "
        "WHERE version = ? "
        "LIMIT 1;";

    sqlite3_stmt* statement = nullptr;

    const int prepareResult =
        sqlite3_prepare_v2(
            m_database,
            sql,
            -1,
            &statement,
            nullptr
        );

    if (prepareResult != SQLITE_OK)
    {
        std::cerr
            << "[Database] Failed to check migration: "
            << sqlite3_errmsg(m_database)
            << std::endl;

        return false;
    }

    sqlite3_bind_int(
        statement,
        1,
        version
    );

    const int stepResult =
        sqlite3_step(statement);

    const bool exists =
        stepResult == SQLITE_ROW;

    sqlite3_finalize(statement);

    return exists;
}

bool Database::markMigrationComplete(int version)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_database == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "INSERT INTO schema_migrations(version) "
        "VALUES(?);";

    sqlite3_stmt* statement = nullptr;

    const int prepareResult =
        sqlite3_prepare_v2(
            m_database,
            sql,
            -1,
            &statement,
            nullptr
        );

    if (prepareResult != SQLITE_OK)
    {
        std::cerr
            << "[Database] Failed to prepare migration insert: "
            << sqlite3_errmsg(m_database)
            << std::endl;

        return false;
    }

    sqlite3_bind_int(
        statement,
        1,
        version
    );

    const bool success =
        sqlite3_step(statement)
        == SQLITE_DONE;

    if (!success)
    {
        std::cerr
            << "[Database] Failed to record migration: "
            << sqlite3_errmsg(m_database)
            << std::endl;
    }

    sqlite3_finalize(statement);

    return success;
}