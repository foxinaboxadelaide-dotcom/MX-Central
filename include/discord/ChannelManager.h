#pragma once

#include <dpp/dpp.h>
#include <string>

class ChannelManager
{
public:
    explicit ChannelManager(dpp::cluster& client);

    // Send a message to a specific channel
    void send(std::uint64_t channelId, const std::string& content);

    // Main server logging
    void logUpload(const std::string& message);
    void logDownload(const std::string& message);
    void logRating(const std::string& message);
    void logCommand(const std::string& message);
    void logSystem(const std::string& message);
    void logError(const std::string& message);

    // Storage server channels
    void pendingUpload(const std::string& message);
    void approvedUpload(const std::string& message);
    void rejectedUpload(const std::string& message);

private:
    dpp::cluster& m_client;
};