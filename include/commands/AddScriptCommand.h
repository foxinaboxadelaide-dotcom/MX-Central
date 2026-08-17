#pragma once

#include <dpp/dpp.h>

class Database;

namespace AddScriptCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    );

    void open(
        const dpp::slashcommand_t& event
    );
}