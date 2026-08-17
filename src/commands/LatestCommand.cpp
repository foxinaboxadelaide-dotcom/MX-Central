#include "commands/LatestCommand.h"

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
     * IMPORTANT:
     *
     * This is the SAME select menu ID used by
     * FindCommand.cpp.
     *
     * FindCommand already handles selections from
     * this menu and opens the private config panel.
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
}


namespace LatestCommand
{
    void handle(
        const dpp::slashcommand_t& event,
        SearchService& searchService
    )
    {
        const std::vector<SearchResult> configs =
            searchService.latest(10);


        /*
         * Nothing approved yet.
         */
        if (configs.empty())
        {
            dpp::embed embed;

            embed
                .set_color(MX_PURPLE)

                .set_title(
                    "🆕 Latest MX Configs"
                )

                .set_description(
                    "There are currently no approved "
                    "configs in the MX library."
                )

                .set_footer(
                    dpp::embed_footer()
                    .set_text(
                        "MX Central • Latest Releases"
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
                "🆕 Latest MX Configs"
            )

            .set_description(
                "Showing the **" +
                std::to_string(
                    configs.size()
                ) +
                " newest approved config" +
                (
                    configs.size() == 1
                    ? ""
                    : "s"
                    ) +
                "** in MX Central.\n\n"
                "Choose a config from the dropdown below."
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
                "#" +
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
                "MX Central • Latest Releases"
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
                "Choose a config..."
            )

            .set_min_values(1)

            .set_max_values(1);


        for (
            const SearchResult& config :
            configs
            )
        {
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
                    config.scriptName,
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


        /*
         * /latest itself stays private.
         *
         * Selecting a config then opens the same
         * private config panel used by /find.
         */
        response.set_flags(
            dpp::m_ephemeral
        );


        event.reply(response);
    }
}