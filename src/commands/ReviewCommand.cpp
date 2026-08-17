#include "commands/ReviewCommand.h"

#include "discord/ChannelConfig.h"

#include "services/StorageService.h"
#include "services/UploadService.h"

#include <dpp/dpp.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <variant>

namespace
{
    constexpr uint32_t MX_PURPLE =
        0x8B5CF6;

    const std::string APPROVE_PREFIX =
        "mx_approve:";

    const std::string REJECT_PREFIX =
        "mx_reject:";

    const std::string REJECT_MODAL_PREFIX =
        "mx_reject_modal:";


    /*
     * ============================================================
     * STRING HELPERS
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


    std::string removePrefix(
        const std::string& value,
        const std::string& prefix
    )
    {
        if (
            !startsWith(
                value,
                prefix
            )
            )
        {
            return "";
        }

        return value.substr(
            prefix.length()
        );
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


    std::string getModalValue(
        const dpp::form_submit_t& event,
        const std::string& customId
    )
    {
        for (
            const auto& component :
            event.components
            )
        {
            if (
                component.custom_id !=
                customId
                )
            {
                continue;
            }

            const auto* value =
                std::get_if<std::string>(
                    &component.value
                );

            if (value != nullptr)
            {
                return *value;
            }
        }

        return "";
    }


    /*
     * ============================================================
     * REJECT MODAL ID
     * ============================================================
     *
     * Format:
     *
     * mx_reject_modal:UPLOAD-ID:MESSAGE-ID
     *
     * We store the original pending review message ID
     * so after the modal is submitted MX can edit that
     * exact review card.
     */

    std::string makeRejectModalId(
        const std::string& uploadId,
        std::uint64_t messageId
    )
    {
        return
            REJECT_MODAL_PREFIX +
            uploadId +
            ":" +
            std::to_string(
                messageId
            );
    }


    bool parseRejectModalId(
        const std::string& customId,
        std::string& uploadId,
        std::uint64_t& messageId
    )
    {
        if (
            !startsWith(
                customId,
                REJECT_MODAL_PREFIX
            )
            )
        {
            return false;
        }


        const std::string payload =
            customId.substr(
                REJECT_MODAL_PREFIX.length()
            );


        const std::size_t separator =
            payload.rfind(':');


        if (
            separator ==
            std::string::npos
            )
        {
            return false;
        }


        uploadId =
            payload.substr(
                0,
                separator
            );


        const std::string messageIdText =
            payload.substr(
                separator + 1
            );


        if (
            uploadId.empty() ||
            messageIdText.empty()
            )
        {
            return false;
        }


        try
        {
            messageId =
                std::stoull(
                    messageIdText
                );
        }
        catch (...)
        {
            return false;
        }


        return true;
    }


    /*
     * ============================================================
     * APPROVED REVIEW CARD
     * ============================================================
     */

    dpp::message makeApprovedMessage(
        const UploadReviewResult& result,
        const std::string& reviewerName
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "✅ Config Approved"
            )

            .set_description(
                "This upload has been approved and "
                "added to the MX Central library."
            )

            .add_field(
                "Config",
                "`" +
                result.scriptName +
                "`",
                true
            )

            .add_field(
                "MX ID",
                "`" +
                result.publicId +
                "`",
                true
            )

            .add_field(
                "Game",
                "`" +
                result.game +
                "`",
                true
            )

            .add_field(
                "Platform",
                "`" +
                result.platform +
                "`",
                true
            )

            .add_field(
                "Parts",
                "`" +
                std::to_string(
                    result.totalParts
                ) +
                "`",
                true
            )

            .add_field(
                "Status",
                "🟢 `Approved`",
                true
            )

            .add_field(
                "Approved By",
                "`" +
                reviewerName +
                "`",
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Upload Review Complete"
                )
            );


        dpp::message message;

        message.add_embed(
            embed
        );

        return message;
    }


    /*
     * ============================================================
     * REJECTED REVIEW CARD
     * ============================================================
     */

    dpp::embed makeRejectedEmbed(
        const UploadReviewResult& result,
        const std::string& reviewerName,
        const std::string& reason
    )
    {
        dpp::embed embed;

        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "❌ Config Rejected"
            )

            .set_description(
                "This upload was reviewed and rejected."
            )

            .add_field(
                "Config",
                "`" +
                result.scriptName +
                "`",
                true
            )

            .add_field(
                "Game",
                "`" +
                result.game +
                "`",
                true
            )

            .add_field(
                "Platform",
                "`" +
                result.platform +
                "`",
                true
            )

            .add_field(
                "Parts",
                "`" +
                std::to_string(
                    result.totalParts
                ) +
                "`",
                true
            )

            .add_field(
                "Status",
                "🔴 `Rejected`",
                true
            )

            .add_field(
                "Rejected By",
                "`" +
                reviewerName +
                "`",
                true
            )

            .add_field(
                "Reason",
                reason,
                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Upload Review Complete"
                )
            );


        return embed;
    }
}


namespace ReviewCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        UploadService& uploadService
    )
    {
        /*
         * ============================================================
         * APPROVE / REJECT BUTTONS
         * ============================================================
         */

        bot.on_button_click(
            [&bot, &uploadService](
                const dpp::button_click_t& event
                )
            {
                const bool isApprove =
                    startsWith(
                        event.custom_id,
                        APPROVE_PREFIX
                    );


                const bool isReject =
                    startsWith(
                        event.custom_id,
                        REJECT_PREFIX
                    );


                /*
                 * Ignore buttons belonging to other
                 * parts of MX.
                 */
                if (
                    !isApprove &&
                    !isReject
                    )
                {
                    return;
                }


                /*
                 * Storage server only.
                 */

                if (
                    event.command.guild_id !=
                    MXChannels::STORAGE_SERVER_ID
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ This review action can only "
                            "be used in the MX storage server."
                        )
                    );

                    return;
                }


                /*
                 * Pending uploads channel only.
                 */

                if (
                    event.command.channel_id !=
                    MXChannels::Storage::PENDING_UPLOADS
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Upload reviews must be completed "
                            "inside `#pending-uploads`."
                        )
                    );

                    return;
                }


                const dpp::user& reviewer =
                    event.command.get_issuing_user();


                const std::uint64_t reviewerId =
                    static_cast<std::uint64_t>(
                        reviewer.id
                        );


                const std::string reviewerName =
                    reviewer.username;


                /*
                 * ====================================================
                 * APPROVE
                 * ====================================================
                 */

                if (isApprove)
                {
                    const std::string uploadId =
                        removePrefix(
                            event.custom_id,
                            APPROVE_PREFIX
                        );


                    if (uploadId.empty())
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ MX couldn't determine "
                                "which upload to approve."
                            )
                        );

                        return;
                    }


                    const UploadReviewResult result =
                        uploadService.approveUpload(
                            uploadId,
                            reviewerId,
                            reviewerName
                        );


                    if (!result.success)
                    {
                        event.reply(
                            makeEphemeral(
                                "❌ Approval failed.\n\n"
                                "**Reason:** " +
                                result.error
                            )
                        );

                        return;
                    }


                    /*
                     * Replace pending review card.
                     *
                     * Buttons disappear because the replacement
                     * message has no components.
                     */

                    event.reply(
                        dpp::ir_update_message,
                        makeApprovedMessage(
                            result,
                            reviewerName
                        )
                    );


                    /*
                     * Security audit.
                     */

                    StorageService::log(
                        bot,
                        MXChannels::Storage::SECURITY_EVENTS,
                        "✅ Upload Approved",

                        "**Reviewer:** `" +
                        reviewerName +
                        "`\n"
                        "**Upload ID:** `" +
                        uploadId +
                        "`\n"
                        "**Config:** `" +
                        result.scriptName +
                        "`\n"
                        "**MX ID:** `" +
                        result.publicId +
                        "`"
                    );


                    return;
                }


                /*
                 * ====================================================
                 * REJECT
                 * ====================================================
                 *
                 * Rejecting no longer happens immediately.
                 *
                 * Clicking Reject opens a modal where you type
                 * the actual rejection reason.
                 */

                const std::string uploadId =
                    removePrefix(
                        event.custom_id,
                        REJECT_PREFIX
                    );


                if (uploadId.empty())
                {
                    event.reply(
                        makeEphemeral(
                            "❌ MX couldn't determine "
                            "which upload to reject."
                        )
                    );

                    return;
                }


                /*
                 * Discord provides the originating message
                 * on component interactions.
                 */

                const std::uint64_t reviewMessageId =
                    static_cast<std::uint64_t>(
                        event.command.msg.id
                        );


                if (reviewMessageId == 0)
                {
                    event.reply(
                        makeEphemeral(
                            "❌ MX couldn't identify the "
                            "pending review message."
                        )
                    );

                    return;
                }


                /*
                 * Build rejection modal.
                 */

                dpp::interaction_modal_response modal(
                    makeRejectModalId(
                        uploadId,
                        reviewMessageId
                    ),

                    "Reject MX Config"
                );


                modal.add_component(
                    dpp::component()
                    .set_label(
                        "Reason for rejection"
                    )
                    .set_id(
                        "rejection_reason"
                    )
                    .set_type(
                        dpp::cot_text
                    )
                    .set_placeholder(
                        "e.g. Invalid config, missing parts, "
                        "duplicate upload..."
                    )
                    .set_min_length(3)
                    .set_max_length(500)
                    .set_text_style(
                        dpp::text_paragraph
                    )
                );


                /*
                 * Button interactions inherit dialog(),
                 * so this directly opens the modal.
                 */

                event.dialog(
                    modal
                );
            }
        );


        /*
         * ============================================================
         * REJECTION MODAL SUBMISSION
         * ============================================================
         */

        bot.on_form_submit(
            [&bot, &uploadService](
                const dpp::form_submit_t& event
                )
            {
                /*
                 * Ignore upload metadata modal and any other
                 * modal MX may add later.
                 */

                if (
                    !startsWith(
                        event.custom_id,
                        REJECT_MODAL_PREFIX
                    )
                    )
                {
                    return;
                }


                std::string uploadId;

                std::uint64_t reviewMessageId =
                    0;


                if (
                    !parseRejectModalId(
                        event.custom_id,
                        uploadId,
                        reviewMessageId
                    )
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ MX couldn't process this "
                            "rejection request."
                        )
                    );

                    return;
                }


                /*
                 * Read typed rejection reason.
                 */

                const std::string reason =
                    getModalValue(
                        event,
                        "rejection_reason"
                    );


                if (
                    reason.find_first_not_of(
                        " \t\r\n"
                    ) == std::string::npos
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Please provide a valid "
                            "rejection reason."
                        )
                    );

                    return;
                }


                const dpp::user& reviewer =
                    event.command.get_issuing_user();


                const std::uint64_t reviewerId =
                    static_cast<std::uint64_t>(
                        reviewer.id
                        );


                const std::string reviewerName =
                    reviewer.username;


                /*
                 * ====================================================
                 * DATABASE REJECTION
                 * ====================================================
                 */

                const UploadReviewResult result =
                    uploadService.rejectUpload(
                        uploadId,
                        reviewerId,
                        reviewerName,
                        reason
                    );


                if (!result.success)
                {
                    event.reply(
                        makeEphemeral(
                            "❌ MX couldn't reject this config.\n\n"
                            "**Reason:** " +
                            result.error
                        )
                    );

                    return;
                }


                /*
                 * ====================================================
                 * PRIVATE CONFIRMATION
                 * ====================================================
                 */

                dpp::embed confirmationEmbed;

                confirmationEmbed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "❌ Config Rejected"
                    )

                    .set_description(
                        "**" +
                        result.scriptName +
                        "** has been rejected successfully."
                    )

                    .add_field(
                        "Reason",
                        reason,
                        false
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Upload Review"
                        )
                    );


                dpp::message confirmation;

                confirmation.add_embed(
                    confirmationEmbed
                );

                confirmation.set_flags(
                    dpp::m_ephemeral
                );


                /*
                 * Modal submission itself must receive
                 * an interaction response.
                 */

                event.reply(
                    confirmation
                );


                /*
                 * ====================================================
                 * UPDATE ORIGINAL PENDING MESSAGE
                 * ====================================================
                 *
                 * The modal interaction is separate from the original
                 * Reject button interaction, so we saved the pending
                 * message ID inside the modal custom ID.
                 */

                bot.message_get(
                    reviewMessageId,
                    MXChannels::Storage::PENDING_UPLOADS,

                    [&bot,
                    result,
                    reviewerName,
                    reason](
                        const dpp::confirmation_callback_t& callback
                        )
                    {
                        if (callback.is_error())
                        {
                            std::cerr
                                << "[ReviewCommand] Could not find "
                                << "pending review message: "
                                << callback.get_error().message
                                << std::endl;

                            return;
                        }


                        dpp::message reviewMessage =
                            callback.get<dpp::message>();


                        /*
                         * Remove:
                         *
                         * old pending embed
                         * Approve button
                         * Reject button
                         */

                        reviewMessage.set_content(
                            ""
                        );

                        reviewMessage.embeds.clear();

                        reviewMessage.components.clear();


                        /*
                         * Replace with rejected state.
                         */

                        reviewMessage.add_embed(
                            makeRejectedEmbed(
                                result,
                                reviewerName,
                                reason
                            )
                        );


                        bot.message_edit(
                            reviewMessage,

                            [](
                                const dpp::confirmation_callback_t&
                                editCallback
                                )
                            {
                                if (
                                    editCallback.is_error()
                                    )
                                {
                                    std::cerr
                                        << "[ReviewCommand] Could not "
                                        << "update rejected review card: "
                                        << editCallback.get_error().message
                                        << std::endl;
                                }
                            }
                        );
                    }
                );


                /*
                 * ====================================================
                 * SECURITY AUDIT LOG
                 * ====================================================
                 */

                StorageService::log(
                    bot,
                    MXChannels::Storage::SECURITY_EVENTS,
                    "❌ Upload Rejected",

                    "**Reviewer:** `" +
                    reviewerName +
                    "`\n"
                    "**Upload ID:** `" +
                    uploadId +
                    "`\n"
                    "**Config:** `" +
                    result.scriptName +
                    "`\n"
                    "**Reason:** " +
                    reason
                );
            }
        );
    }
}