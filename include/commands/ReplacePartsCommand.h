#pragma once

#include <dpp/dpp.h>

class Database;

namespace ReplacePartsCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    );

    void handle(
        const dpp::slashcommand_t& event
    );
}