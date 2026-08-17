#pragma once

#include <dpp/dpp.h>

class Database;

namespace CommandRegistry
{
    void initialize(
        dpp::cluster& bot,
        Database& database
    );
}