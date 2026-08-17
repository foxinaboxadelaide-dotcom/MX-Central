#pragma once

#include <dpp/dpp.h>
#include <string>

namespace StorageService
{
    /*
     * Initialize the storage service.
     */
    void initialize(dpp::cluster& bot);

    /*
     * Generic storage log.
     */
    void log(
        dpp::cluster& bot,
        dpp::snowflake channel_id,
        const std::string& title,
        const std::string& description
    );

    /*
     * Upload events.
     */
    void logUpload(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& filename,
        const std::string& action
    );

    /*
     * Download events.
     */
    void logDownload(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& filename
    );

    /*
     * Rating events.
     */
    void logRating(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& script,
        int rating
    );

    /*
     * Command events.
     */
    void logCommand(
        dpp::cluster& bot,
        const std::string& username,
        const std::string& command
    );

    /*
     * System events.
     */
    void logSystem(
        dpp::cluster& bot,
        const std::string& title,
        const std::string& description
    );

    /*
     * Error events.
     */
    void logError(
        dpp::cluster& bot,
        const std::string& title,
        const std::string& description
    );
}