#pragma once

#include <dpp/dpp.h>

class SearchService;
class RatingService;

namespace FindCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        SearchService& searchService,
        RatingService& ratingService
    );

    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    );
}
