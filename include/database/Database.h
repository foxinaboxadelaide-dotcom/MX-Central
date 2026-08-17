#pragma once

#include <sqlite3.h>

#include <mutex>
#include <string>

class Database
{
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& databasePath);
    void close();

    bool initialize();
    bool isOpen() const;

    bool execute(const std::string& sql);
    bool executeFile(const std::string& filePath);

    sqlite3* handle();

    long long lastInsertId() const;

private:
    bool createMigrationTable();
    bool runMigrations();
    bool hasMigration(int version);
    bool markMigrationComplete(int version);

private:
    sqlite3* m_database;
    mutable std::mutex m_mutex;
};