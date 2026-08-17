#pragma once

#include <dpp/dpp.h>

class SearchService;

namespace LatestCommand
{
    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    );
}