#include "discord/ChannelManager.h"
#include "discord/ChannelConfig.h"

#include <iostream>

ChannelManager::ChannelManager(dpp::cluster& client)
    : m_client(client)
{
}


// ============================================================
// GENERIC SEND
// ============================================================

void ChannelManager::send(
    std::uint64_t channelId,
    const std::string& content)
{
    if (channelId == 0)
    {
        std::cerr << "[ChannelManager] Invalid channel ID.\n";
        return;
    }

    m_client.message_create(
        dpp::message(channelId, content),
        [](const dpp::confirmation_callback_t& callback)
        {
            if (callback.is_error())
            {
                std::cerr
                    << "[ChannelManager] Failed to send message: "
                    << callback.get_error().message
                    << '\n';
            }
        }
    );
}


// ============================================================
// LOG CHANNELS
// ============================================================

void ChannelManager::logUpload(const std::string& message)
{
    send(
        MXChannels::Storage::UPLOAD_LOGS,
        message
    );
}


void ChannelManager::logDownload(const std::string& message)
{
    send(
        MXChannels::Storage::DOWNLOAD_LOGS,
        message
    );
}


void ChannelManager::logRating(const std::string& message)
{
    send(
        MXChannels::Storage::RATING_LOGS,
        message
    );
}


void ChannelManager::logCommand(const std::string& message)
{
    send(
        MXChannels::Storage::COMMAND_LOGS,
        message
    );
}


void ChannelManager::logSystem(const std::string& message)
{
    send(
        MXChannels::Storage::SYSTEM_LOGS,
        message
    );
}


void ChannelManager::logError(const std::string& message)
{
    send(
        MXChannels::Storage::ERROR_LOGS,
        message
    );
}


// ============================================================
// UPLOAD STATUS CHANNELS
// ============================================================

void ChannelManager::pendingUpload(const std::string& message)
{
    send(
        MXChannels::Storage::PENDING_UPLOADS,
        message
    );
}


void ChannelManager::approvedUpload(const std::string& message)
{
    send(
        MXChannels::Storage::APPROVED_UPLOADS,
        message
    );
}


void ChannelManager::rejectedUpload(const std::string& message)
{
    send(
        MXChannels::Storage::REJECTED_UPLOADS,
        message
    );
}