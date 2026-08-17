#pragma once

#include <dpp/dpp.h>

class Database;

namespace LatestFeedService
{
    void initialize(
        Database& database,
        dpp::cluster& bot
    );

    void refresh();
}