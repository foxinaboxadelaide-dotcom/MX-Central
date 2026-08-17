#include "commands/UploadCommand.h"

#include "discord/ChannelConfig.h"
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


    /*
     * How long temporary channel messages stay visible.
     */

    constexpr uint64_t PROGRESS_DELETE_SECONDS =
        6;

    constexpr uint64_t COMPLETE_DELETE_SECONDS =
        12;

    constexpr uint64_t ERROR_DELETE_SECONDS =
        8;


    /*
     * ============================================================
     * EPHEMERAL MESSAGE
     * ============================================================
     */

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


    /*
     * ============================================================
     * TEMPORARY CHANNEL MESSAGE
     * ============================================================
     *
     * Sends a normal Discord message, then automatically deletes
     * it after the requested number of seconds.
     */

    void sendTemporaryMessage(
        dpp::cluster& bot,
        dpp::message message,
        uint64_t deleteAfterSeconds
    )
    {
        bot.message_create(
            message,

            [&bot, deleteAfterSeconds](
                const dpp::confirmation_callback_t& callback
                )
            {
                if (
                    callback.is_error()
                    )
                {
                    std::cerr
                        << "[UploadCommand] Failed to send temporary message: "
                        << callback.get_error().message
                        << std::endl;


                    return;
                }


                const dpp::message& sentMessage =
                    std::get<dpp::message>(
                        callback.value
                    );


                const dpp::snowflake messageId =
                    sentMessage.id;


                const dpp::snowflake channelId =
                    sentMessage.channel_id;


                /*
                 * DPP timers repeat, so stop the timer after
                 * its first execution.
                 */

                bot.start_timer(
                    [
                        &bot,
                        messageId,
                        channelId
                    ](
                        const dpp::timer& timer
                        )
                    {
                        bot.message_delete(
                            messageId,
                            channelId,

                            [](
                                const dpp::confirmation_callback_t&
                                deleteCallback
                                )
                            {
                                if (
                                    deleteCallback.is_error()
                                    )
                                {
                                    std::cerr
                                        << "[UploadCommand] Failed to auto-delete message: "
                                        << deleteCallback.get_error().message
                                        << std::endl;
                                }
                            }
                        );


                        bot.stop_timer(
                            timer
                        );
                    },

                    deleteAfterSeconds
                );
            }
        );
    }


    /*
     * ============================================================
     * MODAL VALUE
     * ============================================================
     */

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


            if (
                value !=
                nullptr
                )
            {
                return *value;
            }
        }


        return "";
    }


    /*
     * ============================================================
     * UPLOAD START EMBED
     * ============================================================
     */

    dpp::embed makeUploadEmbed(
        const std::string& uploadId,
        const std::string& scriptName,
        const std::string& game,
        const std::string& platform,
        const std::string& sensitivity,
        int totalParts
    )
    {
        dpp::embed embed;


        embed
            .set_color(
                MX_PURPLE
            )

            .set_title(
                "🟣 Upload Session Started"
            )

            .set_description(
                "Your config information has been saved.\n"
                "MX is now waiting for the actual config."
            )

            .add_field(
                "Config",

                "`" +
                scriptName +
                "`",

                true
            )

            .add_field(
                "Game",

                "`" +
                game +
                "`",

                true
            )

            .add_field(
                "Platform",

                "`" +
                platform +
                "`",

                true
            )

            .add_field(
                "Sensitivity",

                "`" +
                sensitivity +
                "`",

                true
            )

            .add_field(
                "Parts",

                "`" +
                std::to_string(
                    totalParts
                ) +
                "`",

                true
            )

            .add_field(
                "Upload ID",

                "`" +
                uploadId +
                "`",

                false
            )

            .add_field(
                "Next Step",

                "Paste **Part 1/" +
                std::to_string(
                    totalParts
                ) +
                "** directly into <#" +
                std::to_string(
                    MXChannels::Main::UPLOAD_CONFIG
                ) +
                ">.",

                false
            )

            .set_footer(
                dpp::embed_footer()
                .set_text(
                    "MX Central • Config Upload"
                )
            );


        return embed;
    }
}


namespace UploadCommand
{
    /*
     * ============================================================
     * OPEN UPLOAD MODAL
     * ============================================================
     */

    void openUploadModal(
        const dpp::slashcommand_t& event
    )
    {
        /*
         * Upload command must be used in #upload-config.
         */

        if (
            event.command.channel_id !=
            MXChannels::Main::UPLOAD_CONFIG
            )
        {
            event.reply(
                makeEphemeral(
                    "❌ Please use `/upload-config` in <#" +
                    std::to_string(
                        MXChannels::Main::UPLOAD_CONFIG
                    ) +
                    ">."
                )
            );


            return;
        }


        /*
         * ========================================================
         * UPLOAD METADATA MODAL
         * ========================================================
         */

        dpp::interaction_modal_response modal(
            "mx_upload_metadata",
            "Upload XIM Matrix Config"
        );


        /*
         * CONFIG NAME
         */

        modal.add_component(
            dpp::component()
            .set_label(
                "Config Name"
            )

            .set_id(
                "script_name"
            )

            .set_type(
                dpp::cot_text
            )

            .set_placeholder(
                "e.g. Fortnite Aim Config V3"
            )

            .set_min_length(
                1
            )

            .set_max_length(
                100
            )

            .set_text_style(
                dpp::text_short
            )
        );


        /*
         * GAME
         */

        modal.add_component(
            dpp::component()
            .set_label(
                "Game"
            )

            .set_id(
                "game"
            )

            .set_type(
                dpp::cot_text
            )

            .set_placeholder(
                "e.g. Fortnite"
            )

            .set_min_length(
                1
            )

            .set_max_length(
                100
            )

            .set_text_style(
                dpp::text_short
            )
        );


        /*
         * PLATFORM
         *
         * Supported:
         *
         * PS
         * Xbox
         * PC
         * Not Sure
         *
         * UploadService performs the actual platform
         * normalisation/validation.
         */

        modal.add_component(
            dpp::component()
            .set_label(
                "Platform"
            )

            .set_id(
                "platform"
            )

            .set_type(
                dpp::cot_text
            )

            .set_placeholder(
                "PS / Xbox / PC / Not Sure"
            )

            .set_min_length(
                2
            )

            .set_max_length(
                20
            )

            .set_text_style(
                dpp::text_short
            )
        );


        /*
         * SENSITIVITY
         */

        modal.add_component(
            dpp::component()
            .set_label(
                "In-Game Sensitivity"
            )

            .set_id(
                "sensitivity"
            )

            .set_type(
                dpp::cot_text
            )

            .set_placeholder(
                "Enter sensitivity or type None"
            )

            .set_min_length(
                1
            )

            .set_max_length(
                100
            )

            .set_text_style(
                dpp::text_short
            )
        );


        /*
         * NUMBER OF PARTS
         */

        modal.add_component(
            dpp::component()
            .set_label(
                "Number of Config Parts"
            )

            .set_id(
                "part_count"
            )

            .set_type(
                dpp::cot_text
            )

            .set_placeholder(
                "1 - 20"
            )

            .set_min_length(
                1
            )

            .set_max_length(
                2
            )

            .set_text_style(
                dpp::text_short
            )
        );


        /*
         * Open the modal.
         */

        event.dialog(
            modal
        );
    }


    /*
     * ============================================================
     * REGISTER UPLOAD HANDLERS
     * ============================================================
     */

    void registerHandlers(
        dpp::cluster& bot,
        UploadService& uploadService
    )
    {
        /*
         * ========================================================
         * MODAL SUBMISSION
         * ========================================================
         */

        bot.on_form_submit(
            [&uploadService](
                const dpp::form_submit_t& event
                )
            {
                /*
                 * Ignore modals belonging to other MX systems.
                 */

                if (
                    event.custom_id !=
                    "mx_upload_metadata"
                    )
                {
                    return;
                }


                /*
                 * ====================================================
                 * READ METADATA
                 * ====================================================
                 */

                const std::string scriptName =
                    getModalValue(
                        event,
                        "script_name"
                    );


                const std::string game =
                    getModalValue(
                        event,
                        "game"
                    );


                const std::string platform =
                    getModalValue(
                        event,
                        "platform"
                    );


                std::string sensitivity =
                    getModalValue(
                        event,
                        "sensitivity"
                    );


                const std::string partCountText =
                    getModalValue(
                        event,
                        "part_count"
                    );


                /*
                 * Default sensitivity.
                 */

                if (
                    sensitivity.empty()
                    )
                {
                    sensitivity =
                        "None";
                }


                /*
                 * Required fields.
                 */

                if (
                    scriptName.empty() ||
                    game.empty() ||
                    platform.empty() ||
                    partCountText.empty()
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ One or more required upload fields were missing."
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * PART COUNT
                 * ====================================================
                 */

                int partCount =
                    0;


                try
                {
                    partCount =
                        std::stoi(
                            partCountText
                        );
                }
                catch (...)
                {
                    event.reply(
                        makeEphemeral(
                            "❌ **Number of Config Parts** must be "
                            "a number from `1` to `20`."
                        )
                    );


                    return;
                }


                if (
                    partCount < 1 ||
                    partCount > 20
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ Config parts must be between "
                            "`1` and `20`."
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * USER
                 * ====================================================
                 */

                const dpp::user& user =
                    event.command.get_issuing_user();


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        user.id
                        );


                const std::string username =
                    user.username;


                /*
                 * ====================================================
                 * START UPLOAD
                 * ====================================================
                 *
                 * UploadService validates and normalises platform
                 * values including:
                 *
                 * PS
                 * Xbox
                 * PC
                 * Not Sure
                 */

                const UploadStartResult result =
                    uploadService.startUpload(
                        userId,
                        username,
                        scriptName,
                        game,
                        platform,
                        sensitivity,
                        partCount
                    );


                if (
                    !result.success
                    )
                {
                    event.reply(
                        makeEphemeral(
                            "❌ MX couldn't start your upload.\n\n"
                            "**Reason:** " +
                            result.error
                        )
                    );


                    return;
                }


                /*
                 * ====================================================
                 * PRIVATE SESSION CONFIRMATION
                 * ====================================================
                 *
                 * Only the uploader can see this.
                 */

                dpp::message response;


                response.add_embed(
                    makeUploadEmbed(
                        result.uploadId,
                        scriptName,
                        game,
                        platform,
                        sensitivity,
                        partCount
                    )
                );


                response.set_flags(
                    dpp::m_ephemeral
                );


                event.reply(
                    response
                );
            }
        );


        /*
         * ========================================================
         * CONFIG PART COLLECTION
         * ========================================================
         */

        bot.on_message_create(
            [&bot, &uploadService](
                const dpp::message_create_t& event
                )
            {
                /*
                 * Ignore bots.
                 */

                if (
                    event.msg.author.is_bot()
                    )
                {
                    return;
                }


                /*
                 * Main MX server only.
                 */

                if (
                    event.msg.guild_id !=
                    MXChannels::MAIN_SERVER_ID
                    )
                {
                    return;
                }


                /*
                 * Upload channel only.
                 */

                if (
                    event.msg.channel_id !=
                    MXChannels::Main::UPLOAD_CONFIG
                    )
                {
                    return;
                }


                const std::uint64_t userId =
                    static_cast<std::uint64_t>(
                        event.msg.author.id
                        );


                /*
                 * Don't touch normal messages unless this user
                 * currently has an upload session.
                 */

                if (
                    !uploadService.hasActiveSession(
                        userId
                    )
                    )
                {
                    return;
                }


                /*
                 * ====================================================
                 * EMPTY CONFIG
                 * ====================================================
                 */

                if (
                    event.msg.content.empty()
                    )
                {
                    dpp::message errorMessage;


                    errorMessage.set_channel_id(
                        MXChannels::Main::UPLOAD_CONFIG
                    );


                    errorMessage.set_content(
                        "❌ <@" +
                        std::to_string(
                            userId
                        ) +
                        "> please paste the actual config text."
                    );


                    sendTemporaryMessage(
                        bot,
                        errorMessage,
                        ERROR_DELETE_SECONDS
                    );


                    return;
                }


                /*
                 * ====================================================
                 * SAVE PART
                 * ====================================================
                 */

                const UploadPartResult result =
                    uploadService.capturePart(
                        userId,
                        event.msg.content
                    );


                if (
                    !result.handled
                    )
                {
                    return;
                }


                /*
                 * ====================================================
                 * SUCCESSFUL CONFIG PASTE
                 * ====================================================
                 *
                 * Only delete the user's pasted config after
                 * UploadService confirms it was safely stored.
                 */

                if (
                    result.success
                    )
                {
                    bot.message_delete(
                        event.msg.id,
                        event.msg.channel_id,

                        [](
                            const dpp::confirmation_callback_t&
                            callback
                            )
                        {
                            if (
                                callback.is_error()
                                )
                            {
                                std::cerr
                                    << "[UploadCommand] Could not delete config paste: "
                                    << callback.get_error().message
                                    << std::endl;
                            }
                        }
                    );
                }


                /*
                 * ====================================================
                 * SAVE FAILED
                 * ====================================================
                 *
                 * Do NOT delete the user's pasted config if the
                 * database save failed.
                 */

                if (
                    !result.success
                    )
                {
                    dpp::message errorResponse;


                    errorResponse.set_channel_id(
                        MXChannels::Main::UPLOAD_CONFIG
                    );


                    errorResponse.set_content(
                        "❌ <@" +
                        std::to_string(
                            userId
                        ) +
                        "> MX couldn't save that part.\n"
                        "**Reason:** " +
                        result.error
                    );


                    sendTemporaryMessage(
                        bot,
                        errorResponse,
                        ERROR_DELETE_SECONDS
                    );


                    return;
                }


                /*
                 * ====================================================
                 * UPLOAD COMPLETE
                 * ====================================================
                 */

                if (
                    result.completed
                    )
                {
                    dpp::embed completeEmbed;


                    completeEmbed
                        .set_color(
                            MX_PURPLE
                        )

                        .set_title(
                            "🟣 Config Upload Complete"
                        )

                        .set_description(
                            "<@" +
                            std::to_string(
                                userId
                            ) +
                            ">, your config has been successfully uploaded."
                        )

                        .add_field(
                            "Upload ID",

                            "`" +
                            result.uploadId +
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

                            "🟡 Pending Approval",

                            false
                        )

                        .add_field(
                            "What Happens Next?",

                            "Your config is now waiting for review "
                            "in the MX storage system.",

                            false
                        )

                        .set_footer(
                            dpp::embed_footer()
                            .set_text(
                                "MX Central • Config Upload"
                            )
                        );


                    dpp::message response;


                    response.set_channel_id(
                        MXChannels::Main::UPLOAD_CONFIG
                    );


                    response.add_embed(
                        completeEmbed
                    );


                    /*
                     * Final confirmation stays for 12 seconds.
                     */

                    sendTemporaryMessage(
                        bot,
                        response,
                        COMPLETE_DELETE_SECONDS
                    );


                    return;
                }


                /*
                 * ====================================================
                 * NEXT PART
                 * ====================================================
                 */

                const int nextPart =
                    result.partNumber +
                    1;


                dpp::embed nextEmbed;


                nextEmbed
                    .set_color(
                        MX_PURPLE
                    )

                    .set_title(
                        "✅ Config Part Saved"
                    )

                    .set_description(
                        "<@" +
                        std::to_string(
                            userId
                        ) +
                        "> **Part " +
                        std::to_string(
                            result.partNumber
                        ) +
                        "/" +
                        std::to_string(
                            result.totalParts
                        ) +
                        "** was saved successfully."
                    )

                    .add_field(
                        "Next",

                        "Paste **Part " +
                        std::to_string(
                            nextPart
                        ) +
                        "/" +
                        std::to_string(
                            result.totalParts
                        ) +
                        "** now.",

                        false
                    )

                    .set_footer(
                        dpp::embed_footer()
                        .set_text(
                            "MX Central • Config Upload"
                        )
                    );


                dpp::message response;


                response.set_channel_id(
                    MXChannels::Main::UPLOAD_CONFIG
                );


                response.add_embed(
                    nextEmbed
                );


                /*
                 * Progress notice stays for 6 seconds.
                 */

                sendTemporaryMessage(
                    bot,
                    response,
                    PROGRESS_DELETE_SECONDS
                );
            }
        );
    }
}