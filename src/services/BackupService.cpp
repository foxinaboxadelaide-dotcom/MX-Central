#include "services/BackupService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    constexpr int AUTO_BACKUP_SECONDS =
        21600;


    const std::filesystem::path DATABASE_BACKUP_DIRECTORY =
        "data/backups/database";


    const std::filesystem::path CONFIG_BACKUP_DIRECTORY =
        "data/backups/configs";


    const std::filesystem::path EXPORT_DIRECTORY =
        "data/backups/exports";


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


        return reinterpret_cast<
            const char*
        >(
            value
            );
    }


    dpp::message makeEphemeral(
        const std::string& content
    )
    {
        dpp::message message;

        message.set_content(
            content
        );

        message.set_flags(
            dpp::m_ephemeral
        );


        return message;
    }
}


/*
 * ================================================================
 * CONSTRUCTOR
 * ================================================================
 */

BackupService::BackupService(
    Database& database,
    dpp::cluster& bot
)
    : m_database(database),
    m_bot(bot)
{
}


/*
 * ================================================================
 * INITIALIZE
 * ================================================================
 */

void BackupService::initialize()
{
    if (m_initialized)
    {
        return;
    }


    m_initialized = true;


    try
    {
        std::filesystem::create_directories(
            DATABASE_BACKUP_DIRECTORY
        );


        std::filesystem::create_directories(
            CONFIG_BACKUP_DIRECTORY
        );


        std::filesystem::create_directories(
            EXPORT_DIRECTORY
        );
    }
    catch (
        const std::exception& exception
        )
    {
        std::cerr
            << "[Backup] Directory creation failed: "
            << exception.what()
            << std::endl;
    }


    /*
     * ============================================================
     * STARTUP BACKUP
     * ============================================================
     *
     * This proves the backup system is operational immediately
     * instead of waiting six hours for the first backup.
     */

    const BackupResult startupBackup =
        createFullBackup(
            "Bot Startup"
        );


    postBackupFiles(
        startupBackup,
        "Bot Startup"
    );


    postBackupLog(
        startupBackup,
        "Bot Startup"
    );


    /*
     * ============================================================
     * AUTOMATIC BACKUP
     * ============================================================
     *
     * 21600 seconds = 6 hours.
     */

    m_bot.start_timer(
        [this](
            const dpp::timer&
            )
        {
            const BackupResult result =
                createFullBackup(
                    "Automatic 6 Hour Backup"
                );


            postBackupFiles(
                result,
                "Automatic 6 Hour Backup"
            );


            postBackupLog(
                result,
                "Automatic 6 Hour Backup"
            );
        },

        AUTO_BACKUP_SECONDS
    );


    std::cout
        << "[Backup] Backup system started."
        << std::endl;
}


/*
 * ================================================================
 * OWNER CHECK
 * ================================================================
 */

bool BackupService::isOwner(
    std::uint64_t userId
) const
{
    dpp::guild* guild =
        dpp::find_guild(
            MXChannels::MAIN_SERVER_ID
        );


    if (guild == nullptr)
    {
        return false;
    }


    return
        static_cast<std::uint64_t>(
            guild->owner_id
            ) ==
        userId;
}


/*
 * ================================================================
 * /BACKUPNOW
 * ================================================================
 */

void BackupService::handleBackupNow(
    const dpp::slashcommand_t& event
)
{
    const dpp::user& user =
        event.command.get_issuing_user();


    const std::uint64_t userId =
        static_cast<std::uint64_t>(
            user.id
            );


    if (
        !isOwner(
            userId
        )
        )
    {
        event.reply(
            makeEphemeral(
                "❌ `/backupnow` is restricted "
                "to the MX Central owner."
            )
        );

        return;
    }


    if (m_backupRunning.exchange(true))
    {
        event.reply(
            makeEphemeral(
                "⚠️ MX is already creating a backup."
            )
        );

        return;
    }


    const BackupResult result =
        createFullBackup(
            "Manual Owner Backup"
        );


    m_backupRunning =
        false;


    postBackupFiles(
        result,
        "Manual Owner Backup"
    );


    postBackupLog(
        result,
        "Manual Owner Backup"
    );


    if (!result.success)
    {
        event.reply(
            makeEphemeral(
                "❌ MX could not complete the backup.\n\n"
                "**Error:** `" +
                result.error +
                "`"
            )
        );

        return;
    }


    dpp::embed embed;

    embed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "✅ MX Backup Complete"
        )

        .set_description(
            "A fresh database and config-library backup "
            "has been created."
        )

        .add_field(
            "Configs",
            "`" +
            std::to_string(
                result.configCount
            ) +
            "`",
            true
        )

        .add_field(
            "Database",
            "`" +
            formatBytes(
                result.databaseBytes
            ) +
            "`",
            true
        )

        .add_field(
            "Config Export",
            "`" +
            formatBytes(
                result.configExportBytes
            ) +
            "`",
            true
        )

        .add_field(
            "Database Backup",
            "<#" +
            std::to_string(
                static_cast<std::uint64_t>(
                    MXChannels::Storage::DATABASE_BACKUPS
                    )
            ) +
            ">",
            true
        )

        .add_field(
            "Script Backup",
            "<#" +
            std::to_string(
                static_cast<std::uint64_t>(
                    MXChannels::Storage::SCRIPT_BACKUPS
                    )
            ) +
            ">",
            true
        )

        .add_field(
            "Backup Logs",
            "<#" +
            std::to_string(
                static_cast<std::uint64_t>(
                    MXChannels::Storage::BACKUP_LOGS
                    )
            ) +
            ">",
            true
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Backup System"
            )
        );


    dpp::message response;

    response.add_embed(
        embed
    );

    response.set_flags(
        dpp::m_ephemeral
    );


    event.reply(
        response
    );
}


/*
 * ================================================================
 * /EXPORTCONFIGS
 * ================================================================
 */

void BackupService::handleExportConfigs(
    const dpp::slashcommand_t& event
)
{
    const dpp::user& user =
        event.command.get_issuing_user();


    const std::uint64_t userId =
        static_cast<std::uint64_t>(
            user.id
            );


    if (
        !isOwner(
            userId
        )
        )
    {
        event.reply(
            makeEphemeral(
                "❌ `/exportconfigs` is restricted "
                "to the MX Central owner."
            )
        );

        return;
    }


    try
    {
        std::filesystem::create_directories(
            EXPORT_DIRECTORY
        );
    }
    catch (
        const std::exception& exception
        )
    {
        event.reply(
            makeEphemeral(
                "❌ Could not create the export folder.\n\n"
                "`" +
                std::string(
                    exception.what()
                ) +
                "`"
            )
        );

        return;
    }


    const std::string timestamp =
        createTimestamp();


    const std::string fileName =
        "MX_Central_All_Public_Configs_" +
        timestamp +
        ".txt";


    const std::filesystem::path path =
        EXPORT_DIRECTORY /
        fileName;


    int configCount =
        0;


    std::string error;


    if (
        !createConfigExport(
            path.string(),
            true,
            configCount,
            error
        )
        )
    {
        event.reply(
            makeEphemeral(
                "❌ MX couldn't export the config library.\n\n"
                "**Error:** `" +
                error +
                "`"
            )
        );

        return;
    }


    std::string fileContents;


    try
    {
        fileContents =
            dpp::utility::read_file(
                path.string()
            );
    }
    catch (
        const std::exception& exception
        )
    {
        event.reply(
            makeEphemeral(
                "❌ MX created the export but couldn't "
                "read it for download.\n\n"
                "`" +
                std::string(
                    exception.what()
                ) +
                "`"
            )
        );

        return;
    }


    dpp::embed embed;

    embed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "📦 MX Config Library Export"
        )

        .set_description(
            "Your complete public MX config library "
            "is attached below."
        )

        .add_field(
            "Configs Exported",
            "`" +
            std::to_string(
                configCount
            ) +
            "`",
            true
        )

        .add_field(
            "File",
            "`" +
            fileName +
            "`",
            true
        )

        .add_field(
            "Contains",
            "Metadata + every stored config part",
            false
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Owner Export"
            )
        );


    dpp::message response;

    response.add_embed(
        embed
    );


    response.add_file(
        fileName,
        fileContents
    );


    response.set_flags(
        dpp::m_ephemeral
    );


    event.reply(
        response
    );


    /*
     * Record export in backup logs.
     */

    dpp::embed logEmbed;

    logEmbed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "📤 Config Library Exported"
        )

        .add_field(
            "Requested By",
            "`" +
            user.username +
            "`",
            true
        )

        .add_field(
            "Configs",
            "`" +
            std::to_string(
                configCount
            ) +
            "`",
            true
        )

        .add_field(
            "File",
            "`" +
            fileName +
            "`",
            false
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Backup Logs"
            )
        );


    dpp::message logMessage;

    logMessage.set_channel_id(
        MXChannels::Storage::BACKUP_LOGS
    );

    logMessage.add_embed(
        logEmbed
    );


    m_bot.message_create(
        logMessage
    );
}


/*
 * ================================================================
 * CREATE FULL BACKUP
 * ================================================================
 */

BackupResult BackupService::createFullBackup(
    const std::string&
)
{
    BackupResult result;


    try
    {
        std::filesystem::create_directories(
            DATABASE_BACKUP_DIRECTORY
        );


        std::filesystem::create_directories(
            CONFIG_BACKUP_DIRECTORY
        );
    }
    catch (
        const std::exception& exception
        )
    {
        result.error =
            exception.what();


        return result;
    }


    const std::string timestamp =
        createTimestamp();


    result.databaseFileName =
        "MX_Central_Database_" +
        timestamp +
        ".db";


    result.configExportFileName =
        "MX_Central_All_Configs_" +
        timestamp +
        ".txt";


    const std::filesystem::path databasePath =
        DATABASE_BACKUP_DIRECTORY /
        result.databaseFileName;


    const std::filesystem::path configPath =
        CONFIG_BACKUP_DIRECTORY /
        result.configExportFileName;


    result.databasePath =
        databasePath.string();


    result.configExportPath =
        configPath.string();


    /*
     * ============================================================
     * DATABASE SNAPSHOT
     * ============================================================
     */

    std::string databaseError;


    if (
        !createDatabaseSnapshot(
            result.databasePath,
            databaseError
        )
        )
    {
        result.error =
            "Database backup failed: " +
            databaseError;


        return result;
    }


    /*
     * ============================================================
     * CONFIG LIBRARY EXPORT
     * ============================================================
     *
     * Backup export includes PUBLIC AND HIDDEN configs.
     */

    std::string exportError;


    if (
        !createConfigExport(
            result.configExportPath,
            false,
            result.configCount,
            exportError
        )
        )
    {
        result.error =
            "Config export failed: " +
            exportError;


        return result;
    }


    /*
     * ============================================================
     * FILE SIZES
     * ============================================================
     */

    std::error_code fileError;


    result.databaseBytes =
        std::filesystem::file_size(
            databasePath,
            fileError
        );


    fileError.clear();


    result.configExportBytes =
        std::filesystem::file_size(
            configPath,
            fileError
        );


    result.success =
        true;


    return result;
}


/*
 * ================================================================
 * SQLITE DATABASE SNAPSHOT
 * ================================================================
 */

bool BackupService::createDatabaseSnapshot(
    const std::string& destinationPath,
    std::string& error
)
{
    sqlite3* source =
        m_database.handle();


    if (source == nullptr)
    {
        error =
            "MX database is not open.";


        return false;
    }


    sqlite3* destination =
        nullptr;


    const int openResult =
        sqlite3_open_v2(
            destinationPath.c_str(),
            &destination,
            SQLITE_OPEN_READWRITE |
            SQLITE_OPEN_CREATE,
            nullptr
        );


    if (
        openResult !=
        SQLITE_OK
        )
    {
        if (destination != nullptr)
        {
            error =
                sqlite3_errmsg(
                    destination
                );


            sqlite3_close(
                destination
            );
        }
        else
        {
            error =
                "Could not open backup destination.";
        }


        return false;
    }


    sqlite3_backup* backup =
        sqlite3_backup_init(
            destination,
            "main",
            source,
            "main"
        );


    if (backup == nullptr)
    {
        error =
            sqlite3_errmsg(
                destination
            );


        sqlite3_close(
            destination
        );


        return false;
    }


    const int backupResult =
        sqlite3_backup_step(
            backup,
            -1
        );


    const int finishResult =
        sqlite3_backup_finish(
            backup
        );


    if (
        backupResult !=
        SQLITE_DONE
        )
    {
        error =
            sqlite3_errmsg(
                destination
            );


        sqlite3_close(
            destination
        );


        return false;
    }


    if (
        finishResult !=
        SQLITE_OK
        )
    {
        error =
            sqlite3_errmsg(
                destination
            );


        sqlite3_close(
            destination
        );


        return false;
    }


    sqlite3_close(
        destination
    );


    return true;
}


/*
 * ================================================================
 * CONFIG LIBRARY EXPORT
 * ================================================================
 */

bool BackupService::createConfigExport(
    const std::string& destinationPath,
    bool publicOnly,
    int& configCount,
    std::string& error
)
{
    configCount =
        0;


    sqlite3* database =
        m_database.handle();


    if (database == nullptr)
    {
        error =
            "MX database is unavailable.";


        return false;
    }


    std::ofstream output(
        destinationPath,
        std::ios::out |
        std::ios::binary |
        std::ios::trunc
    );


    if (!output.is_open())
    {
        error =
            "Could not create export file.";


        return false;
    }


    output
        << "============================================================\n"
        << "MX CENTRAL - COMPLETE CONFIG LIBRARY EXPORT\n"
        << "============================================================\n\n"
        << "Generated: "
        << createTimestamp()
        << "\n";


    if (publicOnly)
    {
        output
            << "Scope: PUBLIC CONFIGS ONLY\n";
    }
    else
    {
        output
            << "Scope: COMPLETE BACKUP - PUBLIC + HIDDEN\n";
    }


    output
        << "\n"
        << "Each config part below contains the exact raw text "
        << "stored by MX Central.\n\n"
        << "============================================================\n\n";


    std::string sql =
        "SELECT "
        "s.id, "
        "s.public_id, "
        "s.script_name, "
        "s.game, "
        "s.platform, "
        "COALESCE(s.sensitivity, 'None'), "
        "COALESCE(s.description, ''), "
        "COALESCE(s.version, ''), "
        "s.download_count, "
        "COALESCE(r.average_rating, 0), "
        "COALESCE(r.rating_count, 0), "
        "s.visibility, "
        "s.created_at "
        "FROM scripts s "
        "LEFT JOIN script_rating_summary r "
        "ON r.script_id = s.id ";


    if (publicOnly)
    {
        sql +=
            "WHERE s.visibility = 'public' ";
    }


    sql +=
        "ORDER BY "
        "LOWER(s.game) ASC, "
        "LOWER(s.script_name) ASC, "
        "s.id ASC;";


    sqlite3_stmt* scriptsStatement =
        nullptr;


    if (
        sqlite3_prepare_v2(
            database,
            sql.c_str(),
            -1,
            &scriptsStatement,
            nullptr
        ) != SQLITE_OK
        )
    {
        error =
            sqlite3_errmsg(
                database
            );


        output.close();


        return false;
    }


    constexpr const char* partsSql =
        "SELECT "
        "part_number, "
        "config_data "
        "FROM script_parts "
        "WHERE script_id = ? "
        "ORDER BY part_number ASC;";


    while (
        sqlite3_step(
            scriptsStatement
        ) == SQLITE_ROW
        )
    {
        const long long scriptId =
            sqlite3_column_int64(
                scriptsStatement,
                0
            );


        const std::string publicId =
            columnText(
                scriptsStatement,
                1
            );


        const std::string scriptName =
            columnText(
                scriptsStatement,
                2
            );


        const std::string game =
            columnText(
                scriptsStatement,
                3
            );


        const std::string platform =
            columnText(
                scriptsStatement,
                4
            );


        const std::string sensitivity =
            columnText(
                scriptsStatement,
                5
            );


        const std::string description =
            columnText(
                scriptsStatement,
                6
            );


        const std::string version =
            columnText(
                scriptsStatement,
                7
            );


        const int downloads =
            sqlite3_column_int(
                scriptsStatement,
                8
            );


        const double rating =
            sqlite3_column_double(
                scriptsStatement,
                9
            );


        const int ratingCount =
            sqlite3_column_int(
                scriptsStatement,
                10
            );


        const std::string visibility =
            columnText(
                scriptsStatement,
                11
            );


        const std::string createdAt =
            columnText(
                scriptsStatement,
                12
            );


        ++configCount;


        output
            << "\n"
            << "############################################################\n"
            << "# CONFIG "
            << configCount
            << "\n"
            << "############################################################\n\n"

            << "MX ID: "
            << publicId
            << "\n"

            << "Name: "
            << scriptName
            << "\n"

            << "Game: "
            << game
            << "\n"

            << "Platform: "
            << platform
            << "\n"

            << "Sensitivity: "
            << sensitivity
            << "\n"

            << "Version: "
            << version
            << "\n"

            << "Visibility: "
            << visibility
            << "\n"

            << "Downloads: "
            << downloads
            << "\n"

            << "Average Rating: "
            << std::fixed
            << std::setprecision(1)
            << rating
            << "/5\n"

            << "Rating Count: "
            << ratingCount
            << "\n"

            << "Created: "
            << createdAt
            << "\n";


        if (!description.empty())
        {
            output
                << "Description: "
                << description
                << "\n";
        }


        output
            << "\n";


        sqlite3_stmt* partsStatement =
            nullptr;


        if (
            sqlite3_prepare_v2(
                database,
                partsSql,
                -1,
                &partsStatement,
                nullptr
            ) != SQLITE_OK
            )
        {
            error =
                sqlite3_errmsg(
                    database
                );


            sqlite3_finalize(
                scriptsStatement
            );


            output.close();


            return false;
        }


        sqlite3_bind_int64(
            partsStatement,
            1,
            scriptId
        );


        int actualParts =
            0;


        while (
            sqlite3_step(
                partsStatement
            ) == SQLITE_ROW
            )
        {
            const int partNumber =
                sqlite3_column_int(
                    partsStatement,
                    0
                );


            const std::string configData =
                columnText(
                    partsStatement,
                    1
                );


            ++actualParts;


            output
                << "==================== PART "
                << partNumber
                << " ====================\n\n";


            /*
             * Exact raw configuration text.
             */

            output.write(
                configData.data(),
                static_cast<std::streamsize>(
                    configData.size()
                    )
            );


            output
                << "\n\n"
                << "================== END PART "
                << partNumber
                << " ==================\n\n";
        }


        sqlite3_finalize(
            partsStatement
        );


        output
            << "Stored Parts: "
            << actualParts
            << "\n\n"

            << "############################################################\n"
            << "# END CONFIG - "
            << publicId
            << "\n"
            << "############################################################\n";
    }


    sqlite3_finalize(
        scriptsStatement
    );


    output
        << "\n\n"
        << "============================================================\n"
        << "END MX CENTRAL CONFIG EXPORT\n"
        << "Total Configs: "
        << configCount
        << "\n"
        << "============================================================\n";


    output.close();


    return true;
}


/*
 * ================================================================
 * SEND BACKUPS TO STORAGE SERVER
 * ================================================================
 */

void BackupService::postBackupFiles(
    const BackupResult& result,
    const std::string& reason
)
{
    if (!result.success)
    {
        return;
    }


    std::string databaseContents;
    std::string configContents;


    try
    {
        databaseContents =
            dpp::utility::read_file(
                result.databasePath
            );


        configContents =
            dpp::utility::read_file(
                result.configExportPath
            );
    }
    catch (
        const std::exception& exception
        )
    {
        std::cerr
            << "[Backup] Could not read backup files: "
            << exception.what()
            << std::endl;


        return;
    }


    /*
     * ============================================================
     * DATABASE BACKUPS
     * ============================================================
     */

    dpp::embed databaseEmbed;

    databaseEmbed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "💾 Database Backup"
        )

        .set_description(
            "Complete MX Central SQLite database snapshot."
        )

        .add_field(
            "Reason",
            "`" +
            reason +
            "`",
            true
        )

        .add_field(
            "Size",
            "`" +
            formatBytes(
                result.databaseBytes
            ) +
            "`",
            true
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Database Backup"
            )
        );


    dpp::message databaseMessage;

    databaseMessage.set_channel_id(
        MXChannels::Storage::DATABASE_BACKUPS
    );


    databaseMessage.add_embed(
        databaseEmbed
    );


    databaseMessage.add_file(
        result.databaseFileName,
        databaseContents
    );


    m_bot.message_create(
        databaseMessage
    );


    /*
     * ============================================================
     * SCRIPT BACKUPS
     * ============================================================
     */

    dpp::embed scriptEmbed;

    scriptEmbed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "📦 Complete Config Backup"
        )

        .set_description(
            "Full MX library export containing metadata "
            "and every stored config part."
        )

        .add_field(
            "Configs",
            "`" +
            std::to_string(
                result.configCount
            ) +
            "`",
            true
        )

        .add_field(
            "Size",
            "`" +
            formatBytes(
                result.configExportBytes
            ) +
            "`",
            true
        )

        .add_field(
            "Scope",
            "`Public + Hidden`",
            true
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Script Backup"
            )
        );


    dpp::message scriptMessage;

    scriptMessage.set_channel_id(
        MXChannels::Storage::SCRIPT_BACKUPS
    );


    scriptMessage.add_embed(
        scriptEmbed
    );


    scriptMessage.add_file(
        result.configExportFileName,
        configContents
    );


    m_bot.message_create(
        scriptMessage
    );


    /*
     * ============================================================
     * CLOUD BACKUPS
     * ============================================================
     *
     * This creates another Discord-hosted copy of both files.
     */

    dpp::embed cloudEmbed;

    cloudEmbed
        .set_color(
            MX_PURPLE
        )

        .set_title(
            "☁️ MX Backup Mirror"
        )

        .set_description(
            "Redundant storage copy of the latest MX backup."
        )

        .add_field(
            "Database",
            "`" +
            result.databaseFileName +
            "`",
            false
        )

        .add_field(
            "Config Library",
            "`" +
            result.configExportFileName +
            "`",
            false
        )

        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Backup Mirror"
            )
        );


    dpp::message cloudMessage;

    cloudMessage.set_channel_id(
        MXChannels::Storage::CLOUD_BACKUPS
    );


    cloudMessage.add_embed(
        cloudEmbed
    );


    cloudMessage.add_file(
        result.databaseFileName,
        databaseContents
    );


    cloudMessage.add_file(
        result.configExportFileName,
        configContents
    );


    m_bot.message_create(
        cloudMessage
    );
}


/*
 * ================================================================
 * BACKUP LOG
 * ================================================================
 */

void BackupService::postBackupLog(
    const BackupResult& result,
    const std::string& reason
)
{
    dpp::embed embed;

    embed.set_color(
        MX_PURPLE
    );


    if (result.success)
    {
        embed
            .set_title(
                "✅ Backup Completed"
            )

            .set_description(
                "MX Central completed a full backup successfully."
            )

            .add_field(
                "Reason",
                "`" +
                reason +
                "`",
                true
            )

            .add_field(
                "Configs",
                "`" +
                std::to_string(
                    result.configCount
                ) +
                "`",
                true
            )

            .add_field(
                "Database Size",
                "`" +
                formatBytes(
                    result.databaseBytes
                ) +
                "`",
                true
            )

            .add_field(
                "Config Export Size",
                "`" +
                formatBytes(
                    result.configExportBytes
                ) +
                "`",
                true
            )

            .add_field(
                "Database File",
                "`" +
                result.databaseFileName +
                "`",
                false
            )

            .add_field(
                "Config File",
                "`" +
                result.configExportFileName +
                "`",
                false
            );
    }
    else
    {
        embed
            .set_title(
                "❌ Backup Failed"
            )

            .set_description(
                "MX Central could not complete the backup."
            )

            .add_field(
                "Reason",
                "`" +
                reason +
                "`",
                true
            )

            .add_field(
                "Error",
                result.error.empty()
                ? "Unknown backup error."
                : result.error,
                false
            );
    }


    embed.set_footer(
        dpp::embed_footer()
        .set_text(
            "MX Central • Backup Logs"
        )
    );


    dpp::message message;

    message.set_channel_id(
        MXChannels::Storage::BACKUP_LOGS
    );


    message.add_embed(
        embed
    );


    m_bot.message_create(
        message
    );
}


/*
 * ================================================================
 * TIMESTAMP
 * ================================================================
 */

std::string BackupService::createTimestamp() const
{
    const std::time_t now =
        std::time(
            nullptr
        );


    std::tm localTime{};


#ifdef _WIN32

    localtime_s(
        &localTime,
        &now
    );

#else

    localtime_r(
        &now,
        &localTime
    );

#endif


    std::ostringstream stream;


    stream
        << std::put_time(
            &localTime,
            "%Y-%m-%d_%H-%M-%S"
        );


    return stream.str();
}


/*
 * ================================================================
 * FORMAT BYTES
 * ================================================================
 */

std::string BackupService::formatBytes(
    std::uintmax_t bytes
) const
{
    const double value =
        static_cast<double>(
            bytes
            );


    std::ostringstream stream;


    stream
        << std::fixed
        << std::setprecision(2);


    if (
        bytes >=
        1024ULL * 1024ULL * 1024ULL
        )
    {
        stream
            << value /
            (
                1024.0 *
                1024.0 *
                1024.0
                )
            << " GB";
    }
    else if (
        bytes >=
        1024ULL * 1024ULL
        )
    {
        stream
            << value /
            (
                1024.0 *
                1024.0
                )
            << " MB";
    }
    else if (
        bytes >=
        1024ULL
        )
    {
        stream
            << value /
            1024.0
            << " KB";
    }
    else
    {
        stream
            << bytes
            << " B";
    }


    return stream.str();
}