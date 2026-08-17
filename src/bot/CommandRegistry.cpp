#include "bot/CommandRegistry.h"

#include "commands/AddScriptCommand.h"
#include "commands/AdminCommand.h"
#include "commands/FindCommand.h"
#include "commands/HelpCommand.h"
#include "commands/LatestCommand.h"
#include "commands/ReplacePartsCommand.h"
#include "commands/ReviewCommand.h"
#include "commands/TrendingCommand.h"
#include "commands/UploadCommand.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"

#include "services/BackupService.h"
#include "services/RatingService.h"
#include "services/SearchService.h"
#include "services/StatisticsService.h"
#include "services/StatusService.h"
#include "services/TrendingFeedService.h"
#include "services/UploadService.h"

#include <dpp/dpp.h>

#include <iostream>
#include <memory>
#include <string>

namespace
{
    std::unique_ptr<UploadService>
        g_uploadService;


    std::unique_ptr<SearchService>
        g_searchService;


    std::unique_ptr<RatingService>
        g_ratingService;


    std::unique_ptr<TrendingFeedService>
        g_trendingFeedService;


    std::unique_ptr<StatisticsService>
        g_statisticsService;


    std::unique_ptr<StatusService>
        g_statusService;


    std::unique_ptr<BackupService>
        g_backupService;
}


namespace CommandRegistry
{
    void initialize(
        dpp::cluster& bot,
        Database& database
    )
    {
        /*
         * ============================================================
         * SERVICES
         * ============================================================
         */

        g_uploadService =
            std::make_unique<UploadService>(
                database,
                bot
            );


        g_searchService =
            std::make_unique<SearchService>(
                database
            );


        g_ratingService =
            std::make_unique<RatingService>(
                database
            );


        g_trendingFeedService =
            std::make_unique<TrendingFeedService>(
                database,
                bot,
                *g_searchService
            );


        g_statisticsService =
            std::make_unique<StatisticsService>(
                database,
                bot
            );


        g_statusService =
            std::make_unique<StatusService>(
                database,
                bot
            );


        g_backupService =
            std::make_unique<BackupService>(
                database,
                bot
            );


        /*
         * ============================================================
         * EVENT HANDLERS
         * ============================================================
         */

        UploadCommand::registerHandlers(
            bot,
            *g_uploadService
        );


        ReviewCommand::registerHandlers(
            bot,
            *g_uploadService
        );


        FindCommand::registerHandlers(
            bot,
            *g_searchService,
            *g_ratingService
        );


        HelpCommand::initialize(
            bot,
            database
        );


        AddScriptCommand::registerHandlers(
            bot,
            database
        );


        AdminCommand::registerHandlers(
            bot,
            database
        );


        ReplacePartsCommand::registerHandlers(
            bot,
            database
        );


        /*
         * ============================================================
         * READY
         * ============================================================
         */

        bot.on_ready(
            [&bot](
                const dpp::ready_t&
                )
            {
                if (
                    !dpp::run_once<
                    struct mx_command_registration
                    >()
                    )
                {
                    return;
                }


                std::cout
                    << "[Commands] Registering MX commands..."
                    << std::endl;


                /*
                 * ====================================================
                 * /find
                 * ====================================================
                 */

                dpp::slashcommand findCommand(
                    "find",
                    "Find a XIM Matrix config",
                    bot.me.id
                );


                findCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "script-or-game",
                        "Script name or game",
                        true
                    )
                );


                /*
                 * ====================================================
                 * /removeconfig
                 * ====================================================
                 */

                dpp::slashcommand removeConfigCommand(
                    "removeconfig",
                    "Owner: remove a config from MX Central",
                    bot.me.id
                );


                removeConfigCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "mx-id",
                        "MX config ID, e.g. MX-000001",
                        true
                    )
                );


                removeConfigCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "reason",
                        "Reason for removing this config",
                        true
                    )
                );


                /*
                 * ====================================================
                 * /restoreconfig
                 * ====================================================
                 */

                dpp::slashcommand restoreConfigCommand(
                    "restoreconfig",
                    "Owner: restore a removed MX config",
                    bot.me.id
                );


                restoreConfigCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "mx-id",
                        "MX config ID, e.g. MX-000001",
                        true
                    )
                );


                /*
                 * ====================================================
                 * /editconfig
                 * ====================================================
                 */

                dpp::slashcommand editConfigCommand(
                    "editconfig",
                    "Owner: edit an existing MX config",
                    bot.me.id
                );


                editConfigCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "mx-id",
                        "MX config ID, e.g. MX-000001",
                        true
                    )
                );


                /*
                 * ====================================================
                 * /replaceparts
                 * ====================================================
                 */

                dpp::slashcommand replacePartsCommand(
                    "replaceparts",
                    "Owner: replace the stored parts of an MX config",
                    bot.me.id
                );


                replacePartsCommand.add_option(
                    dpp::command_option(
                        dpp::co_string,
                        "mx-id",
                        "MX config ID, e.g. MX-000001",
                        true
                    )
                );


                replacePartsCommand.add_option(
                    dpp::command_option(
                        dpp::co_integer,
                        "parts",
                        "Number of replacement config parts, 1-20",
                        true
                    )
                );


                /*
                 * ====================================================
                 * REGISTER ALL COMMANDS
                 * ====================================================
                 */

                bot.guild_bulk_command_create(
                    {
                        dpp::slashcommand(
                            "upload-config",
                            "Upload a XIM Matrix config to MX Central",
                            bot.me.id
                        ),

                        findCommand,

                        dpp::slashcommand(
                            "latest",
                            "View the newest MX configs",
                            bot.me.id
                        ),

                        dpp::slashcommand(
                            "trending",
                            "View trending MX configs",
                            bot.me.id
                        ),

                        dpp::slashcommand(
                            "help",
                            "Open the MX Central help centre",
                            bot.me.id
                        ),

                        dpp::slashcommand(
                            "addscript",
                            "Owner: directly add a config to MX Central",
                            bot.me.id
                        ),

                        removeConfigCommand,

                        restoreConfigCommand,

                        editConfigCommand,

                        replacePartsCommand,

                        dpp::slashcommand(
                            "backupnow",
                            "Owner: create a full MX backup now",
                            bot.me.id
                        ),

                        dpp::slashcommand(
                            "exportconfigs",
                            "Owner: download the complete MX config library",
                            bot.me.id
                        )
                    },

                    MXChannels::MAIN_SERVER_ID
                );


                /*
                 * ====================================================
                 * LIVE TRENDING
                 * ====================================================
                 */

                if (
                    g_trendingFeedService !=
                    nullptr
                    )
                {
                    g_trendingFeedService
                        ->initialize();
                }


                /*
                 * ====================================================
                 * LIVE STATISTICS
                 * ====================================================
                 */

                if (
                    g_statisticsService !=
                    nullptr
                    )
                {
                    g_statisticsService
                        ->initialize();
                }


                /*
                 * ====================================================
                 * LIVE BOT STATUS
                 * ====================================================
                 */

                if (
                    g_statusService !=
                    nullptr
                    )
                {
                    g_statusService
                        ->initialize();
                }


                /*
                 * ====================================================
                 * BACKUPS
                 * ====================================================
                 */

                if (
                    g_backupService !=
                    nullptr
                    )
                {
                    g_backupService
                        ->initialize();
                }


                std::cout
                    << "[Commands] MX system ready."
                    << std::endl;
            }
        );


        /*
         * ============================================================
         * SLASH COMMAND ROUTER
         * ============================================================
         */

        bot.on_slashcommand(
            [&database](
                const dpp::slashcommand_t& event
                )
            {
                if (
                    event.command.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    return;
                }


                const std::string command =
                    event.command.get_command_name();


                /*
                 * ====================================================
                 * PUBLIC COMMANDS
                 * ====================================================
                 */

                if (
                    command ==
                    "upload-config"
                    )
                {
                    UploadCommand::openUploadModal(
                        event
                    );


                    return;
                }


                if (
                    command ==
                    "find"
                    )
                {
                    FindCommand::handle(
                        event,
                        *g_searchService
                    );


                    return;
                }


                if (
                    command ==
                    "latest"
                    )
                {
                    LatestCommand::handle(
                        event,
                        *g_searchService
                    );


                    return;
                }


                if (
                    command ==
                    "trending"
                    )
                {
                    TrendingCommand::handle(
                        event,
                        *g_searchService
                    );


                    return;
                }


                if (
                    command ==
                    "help"
                    )
                {
                    HelpCommand::handle(
                        event
                    );


                    return;
                }


                /*
                 * ====================================================
                 * OWNER CONFIG COMMANDS
                 * ====================================================
                 */

                if (
                    command ==
                    "addscript"
                    )
                {
                    AddScriptCommand::open(
                        event
                    );


                    return;
                }


                if (
                    command ==
                    "removeconfig"
                    )
                {
                    AdminCommand::handleRemove(
                        event,
                        database
                    );


                    return;
                }


                if (
                    command ==
                    "restoreconfig"
                    )
                {
                    AdminCommand::handleRestore(
                        event,
                        database
                    );


                    return;
                }


                if (
                    command ==
                    "editconfig"
                    )
                {
                    AdminCommand::handleEdit(
                        event,
                        database
                    );


                    return;
                }


                if (
                    command ==
                    "replaceparts"
                    )
                {
                    ReplacePartsCommand::handle(
                        event
                    );


                    return;
                }


                /*
                 * ====================================================
                 * OWNER BACKUP COMMANDS
                 * ====================================================
                 */

                if (
                    command ==
                    "backupnow"
                    )
                {
                    g_backupService
                        ->handleBackupNow(
                            event
                        );


                    return;
                }


                if (
                    command ==
                    "exportconfigs"
                    )
                {
                    g_backupService
                        ->handleExportConfigs(
                            event
                        );


                    return;
                }
            }
        );
    }
}