#include "bot/EventHandler.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"

#include <dpp/dpp.h>

#include <iostream>
#include <string>

namespace EventHandler
{
    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    )
    {
        /*
         * ============================================================
         * BOT READY
         * ============================================================
         */

        bot.on_ready(
            [&bot, &database](const dpp::ready_t&)
            {
                /*
                 * Prevent startup messages from being repeated if
                 * Discord reconnects the bot.
                 */

                if (!dpp::run_once<struct mx_startup_once>())
                {
                    return;
                }


                std::cout
                    << "========================================"
                    << std::endl;

                std::cout
                    << "MX Central is online."
                    << std::endl;

                std::cout
                    << "Discord connection established."
                    << std::endl;

                std::cout
                    << "========================================"
                    << std::endl;


                /*
                 * ====================================================
                 * MAIN SERVER CHECK
                 * ====================================================
                 */

                bot.guild_get(
                    MXChannels::MAIN_SERVER_ID,

                    [&bot](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (callback.is_error())
                        {
                            std::cerr
                                << "[MX] Main server connection failed: "
                                << callback.get_error().message
                                << std::endl;

                            StorageService::logError(
                                bot,
                                "Main Server Connection Failure",
                                "MX could not access the configured "
                                "MX Central main server."
                            );

                            return;
                        }

                        std::cout
                            << "[MX] Main server connected."
                            << std::endl;
                    }
                );


                /*
                 * ====================================================
                 * STORAGE SERVER CHECK
                 * ====================================================
                 */

                bot.guild_get(
                    MXChannels::STORAGE_SERVER_ID,

                    [&bot, &database](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (callback.is_error())
                        {
                            std::cerr
                                << "[MX] Storage server connection failed: "
                                << callback.get_error().message
                                << std::endl;

                            return;
                        }


                        std::cout
                            << "[MX] Storage server connected."
                            << std::endl;


                        /*
                         * ============================================
                         * CLOUD STATUS CHANNEL
                         * ============================================
                         */

                        bot.channel_get(
                            MXChannels::Storage::CLOUD_STATUS,

                            [&bot, &database](
                                const dpp::confirmation_callback_t&
                                channelCallback
                                )
                            {
                                if (channelCallback.is_error())
                                {
                                    std::cerr
                                        << "[MX] Could not access "
                                        "#cloud-status: "
                                        << channelCallback
                                        .get_error()
                                        .message
                                        << std::endl;

                                    StorageService::logError(
                                        bot,
                                        "Cloud Status Failure",
                                        "MX could not access the configured "
                                        "`#cloud-status` channel."
                                    );

                                    return;
                                }


                                const auto& channel =
                                    std::get<dpp::channel>(
                                        channelCallback.value
                                    );


                                std::cout
                                    << "[MX] Storage channel connected: #"
                                    << channel.name
                                    << std::endl;


                                /*
                                 * ====================================
                                 * CLOUD STATUS EMBED
                                 * ====================================
                                 */

                                const std::string databaseStatus =
                                    database.isOpen()
                                    ? "🟢 Connected"
                                    : "🔴 Offline";


                                dpp::embed embed;

                                embed
                                    .set_color(0x8B5CF6)

                                    .set_title(
                                        "🟣 MX Central — Cloud Online"
                                    )

                                    .set_description(
                                        "MX Central infrastructure has "
                                        "successfully initialized."
                                    )

                                    .add_field(
                                        "Main Server",
                                        "🟢 Connected",
                                        true
                                    )

                                    .add_field(
                                        "Storage Server",
                                        "🟢 Connected",
                                        true
                                    )

                                    .add_field(
                                        "Database",
                                        databaseStatus,
                                        true
                                    )

                                    .add_field(
                                        "Cloud",
                                        "🟢 Operational",
                                        true
                                    )

                                    .add_field(
                                        "Storage",
                                        "🟢 Ready",
                                        true
                                    )

                                    .add_field(
                                        "Discord Gateway",
                                        "🟢 Connected",
                                        true
                                    )

                                    .set_footer(
                                        dpp::embed_footer()
                                        .set_text(
                                            "MX Central • Cloud System"
                                        )
                                    );


                                dpp::message statusMessage;

                                statusMessage.set_channel_id(
                                    MXChannels::Storage::CLOUD_STATUS
                                );

                                statusMessage.add_embed(
                                    embed
                                );


                                /*
                                 * ====================================
                                 * SEND CLOUD STATUS
                                 * ====================================
                                 */

                                bot.message_create(
                                    statusMessage,

                                    [&bot, &database](
                                        const dpp::
                                        confirmation_callback_t&
                                        callback
                                        )
                                    {
                                        if (callback.is_error())
                                        {
                                            std::cerr
                                                << "[MX] Could not send "
                                                "cloud status: "
                                                << callback
                                                .get_error()
                                                .message
                                                << std::endl;

                                            return;
                                        }


                                        std::cout
                                            << "[MX] Cloud status posted."
                                            << std::endl;


                                        /*
                                         * Initialize storage logging.
                                         */

                                        StorageService::initialize(
                                            bot
                                        );


                                        /*
                                         * Log database status.
                                         */

                                        if (database.isOpen())
                                        {
                                            StorageService::logSystem(
                                                bot,
                                                "Database Connected",
                                                "SQLite database "
                                                "`mx_central.db` is online "
                                                "and ready."
                                            );
                                        }
                                        else
                                        {
                                            StorageService::logError(
                                                bot,
                                                "Database Offline",
                                                "MX Central database is "
                                                "not available."
                                            );
                                        }


                                        /*
                                         * Bot console startup entry.
                                         */

                                        StorageService::log(
                                            bot,

                                            MXChannels::Storage::
                                            BOT_CONSOLE,

                                            "🤖 MX Started",

                                            "**Status:** 🟢 Online\n"
                                            "**Database:** 🟢 Connected\n"
                                            "**Storage:** 🟢 Ready\n"
                                            "**Gateway:** 🟢 Connected"
                                        );
                                    }
                                );
                            }
                        );
                    }
                );
            }
        );


        /*
         * ============================================================
         * COMMAND LOGGING
         * ============================================================
         */

        bot.on_slashcommand(
            [&bot](const dpp::slashcommand_t& event)
            {
                /*
                 * Only log commands executed in the main MX server.
                 */

                if (
                    event.command.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    return;
                }


                const std::string commandName =
                    event.command.get_command_name();


                const std::string username =
                    event.command.usr.username;


                std::cout
                    << "[COMMAND] "
                    << username
                    << " used /"
                    << commandName
                    << std::endl;


                StorageService::logCommand(
                    bot,
                    username,
                    "/" + commandName
                );
            }
        );


        /*
         * ============================================================
         * DPP LOGGING
         * ============================================================
         */

        bot.on_log(
            [](const dpp::log_t& event)
            {
                std::cout
                    << "[Discord] "
                    << event.message
                    << std::endl;
            }
        );
    }
}