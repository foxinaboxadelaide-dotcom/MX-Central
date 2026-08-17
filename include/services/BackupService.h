#pragma once

#include <dpp/dpp.h>

#include <atomic>
#include <cstdint>
#include <string>

class Database;

struct BackupResult
{
    bool success = false;

    std::string databasePath;
    std::string configExportPath;

    std::string databaseFileName;
    std::string configExportFileName;

    int configCount = 0;

    std::uintmax_t databaseBytes = 0;
    std::uintmax_t configExportBytes = 0;

    std::string error;
};


class BackupService
{
public:
    BackupService(
        Database& database,
        dpp::cluster& bot
    );

    void initialize();

    void handleBackupNow(
        const dpp::slashcommand_t& event
    );

    void handleExportConfigs(
        const dpp::slashcommand_t& event
    );

private:
    BackupResult createFullBackup(
        const std::string& reason
    );

    bool createDatabaseSnapshot(
        const std::string& destinationPath,
        std::string& error
    );

    bool createConfigExport(
        const std::string& destinationPath,
        bool publicOnly,
        int& configCount,
        std::string& error
    );

    void postBackupFiles(
        const BackupResult& result,
        const std::string& reason
    );

    void postBackupLog(
        const BackupResult& result,
        const std::string& reason
    );

    bool isOwner(
        std::uint64_t userId
    ) const;

    std::string createTimestamp() const;

    std::string formatBytes(
        std::uintmax_t bytes
    ) const;

private:
    Database& m_database;

    dpp::cluster& m_bot;

    bool m_initialized = false;

    std::atomic<bool> m_backupRunning{
        false
    };
};