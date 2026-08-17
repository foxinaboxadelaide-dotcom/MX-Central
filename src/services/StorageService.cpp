#include "services/StorageService.h"
#include "discord/ChannelConfig.h"

#include <dpp/dpp.h>
#include <iostream>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE = 0x8B5CF6;

    void sendLog(
        dpp::cluster& bot,
        dpp::snowflake channel_id,
        const std::string& title,
        const std::string& description
    )
    {
        dpp::embed embed;

        embed
            .set_title(title)
            .set_description(description)
            .set_color(MX_PURPLE)
            .set_footer(
                dpp::embed_footer()
                .set_text("MX Central • Storage System")
            );

        dpp::message message;

        message.set_channel_id(channel_id);
        message.add_embed(embed);

        bot.message_create(
            message,
            [title](const dpp::confirmation_callback_t& callback)
            {
                if (callback.is_error())
                {
                    std::cerr
                        << "[StorageService] Failed to send log '"
                        << title
                        << "': "
                        << callback.get_error().message
                        << std::endl;
                }
            }
        );
    }
}

namespace StorageService
{
    void initialize(dpp::cluster& bot)
    {
        std::cout
            << "[StorageService] Initializing storage system..."
            << std::endl;

        sendLog(
            bot,
            MXChannels::Storage::SYSTEM_LOGS,
            "🟣 Storage System Initialized",
            "MX Central storage and logging infrastructure is online."
        );

        std::cout
            << "[StorageService] Storage system initialized."
            << std::endl;
    }


    void log(
        dpp::cluster& bot,
        dpp::snowflake channel_id,
        const std::string& title,
        const std::string& description
    )
    {
        sendLog(
            bot,
            channel_id,
            title,
            description
        );
    }


    void logUpload(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& filename,
        const std::string& action
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::UPLOAD_LOGS,
            "📤 Upload Activity",
            "**User:** `" + username +
            "`\n**File:** `" + filename +
            "`\n**Action:** `" + action + "`"
        );
    }


    void logDownload(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& filename
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::DOWNLOAD_LOGS,
            "⬇️ Config Download",
            "**User:** `" + username +
            "`\n**File:** `" + filename + "`"
        );
    }


    void logRating(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& script,
        int rating
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::RATING_LOGS,
            "⭐ Script Rating",
            "**User:** `" + username +
            "`\n**Script:** `" + script +
            "`\n**Rating:** `" + std::to_string(rating) +
            "/5`"
        );
    }


    void logCommand(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& command
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::COMMAND_LOGS,
            "🤖 Command Used",
            "**User:** `" + username +
            "`\n**Command:** `" + command + "`"
        );
    }


    void logSystem(
        dpp::cluster& bot,
        const std::string& title,
        const std::string& description
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::SYSTEM_LOGS,
            "⚙️ " + title,
            description
        );
    }


    void logError(
        dpp::cluster& bot,
        const std::string& title,
        const std::string& description
    )
    {
        sendLog(
            bot,
            MXChannels::Storage::ERROR_LOGS,
            "❌ " + title,
            description
        );
    }
}