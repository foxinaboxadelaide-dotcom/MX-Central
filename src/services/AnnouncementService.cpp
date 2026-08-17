#include "services/AnnouncementService.h"

#include "services/StorageService.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;


    /*
     * ============================================================
     * MX CENTRAL IDS
     * ============================================================
     */

    constexpr std::uint64_t MAIN_SERVER_ID =
        1535283967031378010ULL;

    constexpr std::uint64_t STORAGE_SERVER_ID =
        1536296235940581426ULL;

    /*
     * MAIN SERVER:
     * WELCOME -> #announcements
     */

    constexpr std::uint64_t MAIN_ANNOUNCEMENTS_CHANNEL =
        1536289485959077898ULL;

    /*
     * STORAGE SERVER:
     * CONTROL -> #bot-console
     */

    constexpr std::uint64_t STORAGE_BOT_CONSOLE_CHANNEL =
        1536296590506065991ULL;

    /*
     * STORAGE SERVER:
     * LOGS -> #command-logs
     */

    constexpr std::uint64_t STORAGE_COMMAND_LOGS_CHANNEL =
        1538080706096402452ULL;


    /*
     * ============================================================
     * COMMAND / COMPONENT IDS
     * ============================================================
     */

    const std::string COMMAND_NAME =
        "announcement";

    const std::string MODAL_ID =
        "mx_announcement_modal";

    const std::string PUBLISH_PREFIX =
        "mx_announcement_publish:";

    const std::string CANCEL_PREFIX =
        "mx_announcement_cancel:";


    struct PendingAnnouncement
    {
        std::uint64_t userId = 0;
        std::uint64_t nonce = 0;

        std::string username;
        std::string title;
        std::string body;
    };


    std::unordered_map<
        std::uint64_t,
        PendingAnnouncement
    > g_pendingAnnouncements;


    std::mutex g_pendingMutex;


    std::atomic<std::uint64_t>
        g_nonceCounter{ 0 };


    std::atomic<bool>
        g_initialized{ false };


    /*
     * ============================================================
     * HELPERS
     * ============================================================
     */

    bool startsWith(
        const std::string& value,
        const std::string& prefix
    )
    {
        return
            value.rfind(
                prefix,
                0
            ) == 0;
    }


    bool isStorageOwner(
        std::uint64_t userId
    )
    {
        dpp::guild* guild =
            dpp::find_guild(
                STORAGE_SERVER_ID
            );


        if (guild == nullptr)
        {
            return false;
        }


        return
            static_cast<std::uint64_t>(
                guild->owner_id
                ) ==
            userId;
    }


    std::uint64_t parseNonce(
        const std::string& customId,
        const std::string& prefix
    )
    {
        if (
            !startsWith(
                customId,
                prefix
            )
            )
        {
            return 0;
        }


        try
        {
            return std::stoull(
                customId.substr(
                    prefix.size()
                )
            );
        }
        catch (...)
        {
            return 0;
        }
    }


    std::string componentValue(
        const dpp::component& component,
        const std::string& wantedId
    )
    {
        if (
            component.custom_id ==
            wantedId
            )
        {
            const auto* value =
                std::get_if<std::string>(
                    &component.value
                );


            if (
                value !=
                nullptr
                )
            {
                return *value;
            }
        }


        for (
            const dpp::component& child :
            component.components
            )
        {
            const std::string value =
                componentValue(
                    child,
                    wantedId
                );


            if (!value.empty())
            {
                return value;
            }
        }


        return "";
    }


    std::string getModalValue(
        const dpp::form_submit_t& event,
        const std::string& customId
    )
    {
        for (
            const dpp::component& component :
            event.components
            )
        {
            const std::string value =
                componentValue(
                    component,
                    customId
                );


            if (!value.empty())
            {
                return value;
            }
        }


        return "";
    }


    dpp::message makeEphemeral(
        const std::string& content
    )
    {
        dpp::message message;

        message.set_content(
            content
        );

        message.set_flags(
            dpp::m_ephemeral
        );


        return message;
    }


    dpp::message buildPreview(
        const PendingAnnouncement& announcement
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                announcement.title
            )

            .set_description(
                announcement.body
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Announcement"
                )
            );


        dpp::component publishButton;

        publishButton
            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_success
            )

            .set_label(
                "Publish @everyone"
            )

            .set_id(
                PUBLISH_PREFIX +
                std::to_string(
                    announcement.nonce
                )
            );


        dpp::component cancelButton;

        cancelButton
            .set_type(
                dpp::cot_button
            )

            .set_style(
                dpp::cos_danger
            )

            .set_label(
                "Cancel"
            )

            .set_id(
                CANCEL_PREFIX +
                std::to_string(
                    announcement.nonce
                )
            );


        dpp::component row;

        row.add_component(
            publishButton
        );

        row.add_component(
            cancelButton
        );


        dpp::message message;

        message.set_content(
            "📢 **Announcement Preview**\n"
            "Destination: <#" +
            std::to_string(
                MAIN_ANNOUNCEMENTS_CHANNEL
            ) +
            ">\n"
            "Ping: `@everyone`"
        );

        message.add_embed(
            embed
        );

        message.add_component(
            row
        );

        message.set_flags(
            dpp::m_ephemeral
        );


        return message;
    }


    dpp::message buildPublishedConfirmation(
        const PendingAnnouncement& announcement
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "✅ Announcement Published"
            )

            .set_description(
                "**" +
                announcement.title +
                "** was sent to <#" +
                std::to_string(
                    MAIN_ANNOUNCEMENTS_CHANNEL
                ) +
                "> with an `@everyone` ping."
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Announcement System"
                )
            );


        dpp::message message;

        message.add_embed(
            embed
        );


        return message;
    }


    dpp::message buildCancelledConfirmation()
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "❌ Announcement Cancelled"
            )

            .set_description(
                "Nothing was posted to the main MX Central server."
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Announcement System"
                )
            );


        dpp::message message;

        message.add_embed(
            embed
        );


        return message;
    }
}


namespace AnnouncementService
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


        /*
         * ========================================================
         * REGISTER /announcement IN THE PRIVATE STORAGE SERVER
         * ========================================================
         */

        bot.on_ready(
            [&bot](
                const dpp::ready_t&
                )
            {
                if (
                    !dpp::run_once<
                    struct mx_announcement_registration
                    >()
                    )
                {
                    return;
                }


                dpp::slashcommand command(
                    COMMAND_NAME,
                    "Owner: create a public MX Central announcement",
                    bot.me.id
                );


                bot.guild_command_create(
                    command,
                    STORAGE_SERVER_ID,

                    [](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (
                            callback.is_error()
                            )
                        {
                            std::cerr
                                << "[AnnouncementService] Could not register "
                                << "/announcement: "
                                << callback.get_error().message
                                << std::endl;


                            return;
                        }


                        std::cout
                            << "[AnnouncementService] /announcement registered."
                            << std::endl;
                    }
                );
            }
        );


        /*
         * ========================================================
         * /announcement -> OPEN MODAL
         * ========================================================
         */

        bot.on_slashcommand(
            [](
                const dpp::slashcommand_t& event
                )
            {
                if (
                    static_cast<std::uint64_t>(
                        event.command.guild_id
                        ) !=
                    STORAGE_SERVER_ID
                    )
                {
                    return;
                }


                if (
                    event.command.get_command_name() !=
                    COMMAND_NAME
                    )
                {
                    return;
                }


                const dpp::user& user =
                    event.command.get_issuing_user();


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        user.id
                        );


                /*
                 * Owner-only.
                 *
                 * No owner role ID is required because we read the
                 * actual storage server owner from Discord's guild cache.
                 */

                if (
                    !isStorageOwner(
                        userId
                    )
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ `/announcement` is restricted to "
                            "the MX Central owner."
                        )
                    );


                    return;
                }


                /*
                 * Keep management commands inside private #bot-console.
                 */

                if (
                    static_cast<std::uint64_t>(
                        event.command.channel_id
                        ) !=
                    STORAGE_BOT_CONSOLE_CHANNEL
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "📢 Use `/announcement` inside <#" +
                            std::to_string(
                                STORAGE_BOT_CONSOLE_CHANNEL
                            ) +
                            ">."
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * TITLE + BODY MODAL
                 * ====================================================
                 */

                dpp::interaction_modal_response modal(
                    MODAL_ID,
                    "Create Announcement"
                );


                modal.add_component(
                    dpp::component()
                    .set_label(
                        "Announcement Title"
                    )

                    .set_id(
                        "announcement_title"
                    )

                    .set_type(
                        dpp::cot_text
                    )

                    .set_placeholder(
                        "e.g. MX Central Update"
                    )

                    .set_min_length(
                        1
                    )

                    .set_max_length(
                        256
                    )

                    .set_required(
                        true
                    )

                    .set_text_style(
                        dpp::text_short
                    )
                );


                modal.add_row();


                modal.add_component(
                    dpp::component()
                    .set_label(
                        "Announcement Message"
                    )

                    .set_id(
                        "announcement_body"
                    )

                    .set_type(
                        dpp::cot_text
                    )

                    .set_placeholder(
                        "Write the announcement here..."
                    )

                    .set_min_length(
                        1
                    )

                    .set_max_length(
                        4000
                    )

                    .set_required(
                        true
                    )

                    .set_text_style(
                        dpp::text_paragraph
                    )
                );


                event.dialog(
                    modal
                );
            }
        );


        /*
         * ========================================================
         * MODAL SUBMISSION -> PRIVATE PREVIEW
         * ========================================================
         */

        bot.on_form_submit(
            [](
                const dpp::form_submit_t& event
                )
            {
                if (
                    event.custom_id !=
                    MODAL_ID
                    )
                {
                    return;
                }


                const dpp::user& user =
                    event.command.get_issuing_user();


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        user.id
                        );


                if (
                    !isStorageOwner(
                        userId
                    )
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Only the MX Central owner can "
                            "publish announcements."
                        )
                    );


                    return;
                }


                const std::string title =
                    getModalValue(
                        event,
                        "announcement_title"
                    );


                const std::string body =
                    getModalValue(
                        event,
                        "announcement_body"
                    );


                if (
                    title.empty() ||
                    body.empty()
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Announcement title and message "
                            "are both required."
                        )
                    );


                    return;
                }


                PendingAnnouncement announcement;

                announcement.userId =
                    userId;

                announcement.nonce =
                    ++g_nonceCounter;

                announcement.username =
                    user.username;

                announcement.title =
                    title;

                announcement.body =
                    body;


                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_pendingMutex
                        );


                    g_pendingAnnouncements[userId] =
                        announcement;
                }


                event.reply(
                    buildPreview(
                        announcement
                    )
                );
            }
        );


        /*
         * ========================================================
         * PREVIEW BUTTONS
         * ========================================================
         */

        bot.on_button_click(
            [&bot](
                const dpp::button_click_t& event
                )
            {
                const bool publish =
                    startsWith(
                        event.custom_id,
                        PUBLISH_PREFIX
                    );


                const bool cancel =
                    startsWith(
                        event.custom_id,
                        CANCEL_PREFIX
                    );


                if (
                    !publish &&
                    !cancel
                    )
                {
                    return;
                }


                const dpp::user& user =
                    event.command.get_issuing_user();


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        user.id
                        );


                if (
                    !isStorageOwner(
                        userId
                    )
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Only the MX Central owner can "
                            "use this announcement preview."
                        )
                    );


                    return;
                }


                const std::string& prefix =
                    publish
                    ? PUBLISH_PREFIX
                    : CANCEL_PREFIX;


                const std::uint64_t nonce =
                    parseNonce(
                        event.custom_id,
                        prefix
                    );


                PendingAnnouncement announcement;

                bool valid =
                    false;


                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_pendingMutex
                        );


                    const auto iterator =
                        g_pendingAnnouncements.find(
                            userId
                        );


                    if (
                        iterator !=
                        g_pendingAnnouncements.end() &&
                        iterator->second.nonce ==
                        nonce
                        )
                    {
                        announcement =
                            iterator->second;

                        valid =
                            true;
                    }
                }


                if (!valid)
                {
                    event.reply(
                        makeEphemeral(
                            "❌ This announcement preview expired. "
                            "Run `/announcement` again."
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * CANCEL
                 * ====================================================
                 */

                if (cancel)
                {
                    {
                        std::lock_guard<std::mutex>
                            lock(
                                g_pendingMutex
                            );


                        g_pendingAnnouncements.erase(
                            userId
                        );
                    }


                    event.reply(
                        dpp::ir_update_message,
                        buildCancelledConfirmation()
                    );


                    return;
                }


                /*
                 * ====================================================
                 * PUBLISH
                 * ====================================================
                 *
                 * This is hard-linked to the MAIN MX Central
                 * #announcements channel:
                 *
                 * 1536289485959077898
                 */

                dpp::embed embed;

                embed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        announcement.title
                    )

                    .set_description(
                        announcement.body
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Announcement"
                        )
                    );


                dpp::message publicMessage;

                publicMessage.set_channel_id(
                    MAIN_ANNOUNCEMENTS_CHANNEL
                );


                /*
                 * Discord mentions must be normal message content.
                 */

                publicMessage.set_content(
                    "@everyone"
                );


                /*
                 * Allow only the everyone/here parser.
                 *
                 * Users and role mentions inside the body will not
                 * unexpectedly ping users or roles.
                 */

                publicMessage.set_allowed_mentions(
                    false,
                    false,
                    true,
                    false,
                    std::vector<dpp::snowflake>{},
                    std::vector<dpp::snowflake>{}
                );


                publicMessage.add_embed(
                    embed
                );


                bot.message_create(
                    publicMessage,

                    [&bot, announcement](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (
                            callback.is_error()
                            )
                        {
                            std::cerr
                                << "[AnnouncementService] Publish failed: "
                                << callback.get_error().message
                                << std::endl;


                            StorageService::log(
                                bot,
                                STORAGE_COMMAND_LOGS_CHANNEL,
                                "❌ Announcement Failed",
                                "**Title:** `" +
                                announcement.title +
                                "`\n"
                                "**Requested By:** `" +
                                announcement.username +
                                "`\n"
                                "**Destination:** <#" +
                                std::to_string(
                                    MAIN_ANNOUNCEMENTS_CHANNEL
                                ) +
                                ">\n"
                                "**Error:** `" +
                                callback.get_error().message +
                                "`"
                            );


                            return;
                        }


                        StorageService::log(
                            bot,
                            STORAGE_COMMAND_LOGS_CHANNEL,
                            "📢 Announcement Published",
                            "**Title:** `" +
                            announcement.title +
                            "`\n"
                            "**Published By:** `" +
                            announcement.username +
                            "`\n"
                            "**Destination:** <#" +
                            std::to_string(
                                MAIN_ANNOUNCEMENTS_CHANNEL
                            ) +
                            ">\n"
                            "**Mention:** `@everyone`"
                        );
                    }
                );


                {
                    std::lock_guard<std::mutex>
                        lock(
                            g_pendingMutex
                        );


                    g_pendingAnnouncements.erase(
                        userId
                    );
                }


                event.reply(
                    dpp::ir_update_message,
                    buildPublishedConfirmation(
                        announcement
                    )
                );
            }
        );


        std::cout
            << "[AnnouncementService] /announcement system enabled."
            << std::endl;
    }
}
