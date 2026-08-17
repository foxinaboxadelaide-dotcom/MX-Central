#pragma once

#include <dpp/dpp.h>

class SearchService;

namespace TrendingCommand
{
    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    );
}