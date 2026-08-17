#pragma once

#include <dpp/dpp.h>

class Database;

namespace EventHandler
{
    void registerHandlers(
        dpp::cluster& bot,
        Database& database
    );
}