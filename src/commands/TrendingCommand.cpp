#include "commands/TrendingCommand.h"

#include "services/SearchService.h"

#include <dpp/dpp.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    /*
     * Same ID used by FindCommand.cpp.
     *
     * This means the existing config selector
     * automatically handles /trending selections.
     */
    const std::string CONFIG_SELECT_MENU_ID =
        "mx_config_select";


    std::string ratingText(
        double average,
        int count
    )
    {
        if (count <= 0)
        {
            return "Not rated";
        }

        std::ostringstream stream;

        stream
            << std::fixed
            << std::setprecision(1)
            << average
            << "/5";

        return stream.str();
    }


    std::string rankingIcon(
        std::size_t index
    )
    {
        if (index == 0)
        {
            return "🥇";
        }

        if (index == 1)
        {
            return "🥈";
        }

        if (index == 2)
        {
            return "🥉";
        }

        return "🔥";
    }
}


namespace TrendingCommand
{
    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    )
    {
        const std::vector<SearchResult> configs =
            searchService.trending(10);


        /*
         * No configs.
         */
        if (configs.empty())
        {
            dpp::embed embed;

            embed
                .set_color(MX_PURPLE)

                .set_title(
                    "🔥 Trending MX Configs"
                )

                .set_description(
                    "There aren't any approved configs "
                    "to rank yet."
                )

                .set_footer(
                    dpp::embed_footer()
                    .set_text(
                        "MX Central • Trending"
                    )
                );


            dpp::message response;

            response.add_embed(embed);

            response.set_flags(
                dpp::m_ephemeral
            );


            event.reply(response);

            return;
        }


        /*
         * ========================================================
         * EMBED
         * ========================================================
         */

        dpp::embed embed;

        embed
            .set_color(MX_PURPLE)

            .set_title(
                "🔥 Trending MX Configs"
            )

            .set_description(
                "The **" +
                std::to_string(
                    configs.size()
                ) +
                " hottest config" +
                (
                    configs.size() == 1
                    ? ""
                    : "s"
                    ) +
                "** in MX Central right now.\n\n"
                "Rankings use **downloads and community ratings**.\n"
                "Choose a config below to open it."
            );


        for (
            std::size_t index = 0;
            index < configs.size();
            ++index
            )
        {
            const SearchResult& config =
                configs[index];


            embed.add_field(
                rankingIcon(index) +
                " #" +
                std::to_string(
                    index + 1
                ) +
                " • " +
                config.scriptName,

                "**Game:** `" +
                config.game +
                "`\n"
                "**Platform:** `" +
                config.platform +
                "`\n"
                "**Downloads:** `" +
                std::to_string(
                    config.downloadCount
                ) +
                "`\n"
                "**Rating:** `" +
                ratingText(
                    config.averageRating,
                    config.ratingCount
                ) +
                "`",

                false
            );
        }


        embed.set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Trending Configs"
            )
        );


        /*
         * ========================================================
         * DROPDOWN
         * ========================================================
         */

        dpp::component selectMenu;

        selectMenu
            .set_type(
                dpp::cot_selectmenu
            )

            .set_id(
                CONFIG_SELECT_MENU_ID
            )

            .set_placeholder(
                "Choose a trending config..."
            )

            .set_min_values(1)

            .set_max_values(1);


        for (
            std::size_t index = 0;
            index < configs.size();
            ++index
            )
        {
            const SearchResult& config =
                configs[index];


            std::string label =
                "#" +
                std::to_string(
                    index + 1
                ) +
                " • " +
                config.scriptName;


            std::string description =
                config.game +
                " • " +
                config.platform +
                " • " +
                std::to_string(
                    config.downloadCount
                ) +
                " downloads";


            selectMenu.add_select_option(
                dpp::select_option(
                    label,
                    config.publicId,
                    description
                )
            );
        }


        dpp::component row;

        row.add_component(
            selectMenu
        );


        /*
         * ========================================================
         * RESPONSE
         * ========================================================
         */

        dpp::message response;

        response.add_embed(
            embed
        );

        response.add_component(
            row
        );


        response.set_flags(
            dpp::m_ephemeral
        );


        event.reply(response);
    }
}