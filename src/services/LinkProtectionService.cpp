#include "services/LinkProtectionService.h"

#include "services/StorageService.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <regex>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    constexpr std::uint64_t MAIN_SERVER_ID =
        1535283967031378010ULL;


    constexpr std::uint64_t STORAGE_SECURITY_EVENTS_CHANNEL =
        1538080965627486309ULL;


    constexpr uint64_t WARNING_DELETE_SECONDS =
        6;


    std::atomic<bool>
        g_initialized{ false };


    /*
     * ============================================================
     * LINK DETECTION
     * ============================================================
     *
     * Detects common URL / invite forms:
     *
     * https://example.com
     * http://example.com
     * www.example.com
     * discord.gg/example
     * discord.com/invite/example
     * example.com
     */

    const std::regex LINK_PATTERN(
        R"((https?://[^\s<]+|www\.[^\s<]+|discord\.gg/[^\s<]+|discord(?:app)?\.com/invite/[^\s<]+|(?:^|[\s(])(?:[a-z0-9-]+\.)+[a-z]{2,24}(?:[/:?#][^\s<]*)?))",
        std::regex::icase
    );


    bool containsLink(
        const std::string& content
    )
    {
        return
            std::regex_search(
                content,
                LINK_PATTERN
            );
    }


    std::string truncateForLog(
        const std::string& content
    )
    {
        constexpr std::size_t MAXIMUM =
            850;


        if (
            content.size() <=
            MAXIMUM
            )
        {
            return content;
        }


        return
            content.substr(
                0,
                MAXIMUM
            ) +
            "...";
    }


    bool isOwnerOrStaff(
        const dpp::message& message
    )
    {
        dpp::guild* guild =
            dpp::find_guild(
                MAIN_SERVER_ID
            );


        if (
            guild ==
            nullptr
            )
        {
            return false;
        }


        const std::uint64_t userId =
            static_cast<std::uint64_t>(
                message.author.id
                );


        /*
         * Main server owner automatically bypasses.
         */

        if (
            static_cast<std::uint64_t>(
                guild->owner_id
                ) ==
            userId
            )
        {
            return true;
        }


        dpp::channel* channel =
            dpp::find_channel(
                message.channel_id
            );


        if (
            channel ==
            nullptr
            )
        {
            return false;
        }


        /*
         * Discord effective permissions are used here.
         *
         * Staff bypass if they have either:
         *
         * - Administrator
         * - Manage Messages
         *
         * This means no staff role IDs are required as long as the
         * staff roles already carry one of those permissions.
         */

        const auto permissions =
            channel->get_user_permissions(
                &message.author
            );


        return
            (
                permissions &
                dpp::p_administrator
                )
            ||
            (
                permissions &
                dpp::p_manage_messages
                );
    }


    void sendTemporaryWarning(
        dpp::cluster& bot,
        std::uint64_t channelId,
        std::uint64_t userId
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🔗 Link Removed"
            )

            .set_description(
                "<@" +
                std::to_string(
                    userId
                ) +
                ">, links are not allowed in MX Central."
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Link Protection"
                )
            );


        dpp::message warning;

        warning.set_channel_id(
            channelId
        );

        warning.add_embed(
            embed
        );


        bot.message_create(
            warning,

            [&bot](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    return;
                }


                const dpp::message created =
                    callback.get<dpp::message>();


                const dpp::snowflake messageId =
                    created.id;


                const dpp::snowflake createdChannelId =
                    created.channel_id;


                bot.start_timer(
                    [
                        &bot,
                        messageId,
                        createdChannelId
                    ](
                        const dpp::timer& timer
                        )
                    {
                        bot.message_delete(
                            messageId,
                            createdChannelId
                        );


                        bot.stop_timer(
                            timer
                        );
                    },

                    WARNING_DELETE_SECONDS
                );
            }
        );
    }
}


namespace LinkProtectionService
{
    void initialize(
        dpp::cluster& bot
    )
    {
        if (
            g_initialized.exchange(
                true
            )
            )
        {
            return;
        }


        bot.on_message_create(
            [&bot](
                const dpp::message_create_t& event
                )
            {
                /*
                 * Do not moderate MX or other bot messages.
                 */

                if (
                    event.msg.author.is_bot()
                    )
                {
                    return;
                }


                /*
                 * MAIN PUBLIC SERVER ONLY.
                 *
                 * The private storage server is completely excluded.
                 */

                if (
                    static_cast<std::uint64_t>(
                        event.msg.guild_id
                        ) !=
                    MAIN_SERVER_ID
                    )
                {
                    return;
                }


                if (
                    event.msg.content.empty() ||
                    !containsLink(
                        event.msg.content
                    )
                    )
                {
                    return;
                }


                /*
                 * Owner / staff bypass.
                 */

                if (
                    isOwnerOrStaff(
                        event.msg
                    )
                    )
                {
                    return;
                }


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        event.msg.author.id
                        );


                const std::uint64_t channelId =
                    static_cast<std::uint64_t>(
                        event.msg.channel_id
                        );


                const std::string username =
                    event.msg.author.username;


                const std::string originalContent =
                    event.msg.content;


                /*
                 * Delete the member's link.
                 */

                bot.message_delete(
                    event.msg.id,
                    event.msg.channel_id,

                    [
                        &bot,
                        userId,
                        channelId,
                        username,
                        originalContent
                    ](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (
                            callback.is_error()
                            )
                        {
                            std::cerr
                                << "[LinkProtection] Could not delete link: "
                                << callback.get_error().message
                                << std::endl;


                            return;
                        }


                        sendTemporaryWarning(
                            bot,
                            channelId,
                            userId
                        );


                        StorageService::log(
                            bot,
                            STORAGE_SECURITY_EVENTS_CHANNEL,
                            "🔗 Link Blocked",
                            "**User:** <@" +
                            std::to_string(
                                userId
                            ) +
                            ">\n"
                            "**Username:** `" +
                            username +
                            "`\n"
                            "**Channel:** <#" +
                            std::to_string(
                                channelId
                            ) +
                            ">\n"
                            "**Message:**\n```" +
                            truncateForLog(
                                originalContent
                            ) +
                            "```"
                        );
                    }
                );
            }
        );


        std::cout
            << "[LinkProtection] Public-server link protection enabled."
            << std::endl;
    }
}
