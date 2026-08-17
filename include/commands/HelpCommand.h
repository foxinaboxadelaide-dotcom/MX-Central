#pragma once

#include <dpp/dpp.h>

class Database;

namespace HelpCommand
{
    /*
     * Both parameter orders are supported so this remains compatible
     * with the existing MX CommandRegistry setup.
     */
    void initialize(
        Database& database,
        dpp::cluster& bot
    );

    void initialize(
        dpp::cluster& bot,
        Database& database
    );

    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    );

    void handle(
        const dpp::slashcommand_t& event
    );
}