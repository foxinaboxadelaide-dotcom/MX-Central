#pragma once

#include <dpp/dpp.h>

class Database;

namespace AdminCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    );

    void handleRemove(
        const dpp::slashcommand_t& event,
        Database& database
    );

    void handleRestore(
        const dpp::slashcommand_t& event,
        Database& database
    );

    void handleEdit(
        const dpp::slashcommand_t& event,
        Database& database
    );
}