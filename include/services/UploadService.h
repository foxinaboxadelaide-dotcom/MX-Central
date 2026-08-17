#pragma once

#include "models/UploadSession.h"

#include <dpp/dpp.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class Database;

struct UploadStartResult
{
    bool success = false;
    std::string uploadId;
    std::string error;
};

struct UploadPartResult
{
    bool handled = false;
    bool success = false;
    bool completed = false;

    int partNumber = 0;
    int totalParts = 0;

    std::string uploadId;
    std::string error;
};

struct UploadReviewResult
{
    bool success = false;

    std::string uploadId;
    std::string publicId;

    std::string scriptName;
    std::string game;
    std::string platform;
    std::string sensitivity;

    std::uint64_t uploaderId = 0;

    int totalParts = 0;

    std::string error;
};

class UploadService
{
public:
    UploadService(
        Database& database,
        dpp::cluster& bot
    );

    UploadStartResult startUpload(
        std::uint64_t userId,
        const std::string& username,
        const std::string& scriptName,
        const std::string& game,
        const std::string& platform,
        const std::string& sensitivity,
        int totalParts
    );

    UploadPartResult capturePart(
        std::uint64_t userId,
        const std::string& configData
    );

    bool hasActiveSession(
        std::uint64_t userId
    ) const;

    UploadReviewResult approveUpload(
        const std::string& uploadId,
        std::uint64_t reviewerId,
        const std::string& reviewerName
    );

    UploadReviewResult rejectUpload(
        const std::string& uploadId,
        std::uint64_t reviewerId,
        const std::string& reviewerName,
        const std::string& reason
    );

private:
    bool ensureUser(
        std::uint64_t userId,
        const std::string& username
    );

    bool insertUpload(
        const UploadSession& session
    );

    bool insertPart(
        const UploadSession& session,
        int partNumber,
        const std::string& configData
    );

    bool finalizeUpload(
        const UploadSession& session
    );

    void postPendingUpload(
        const UploadSession& session
    );

    void incrementUploadStatistics();

    std::string generateUploadId();

    std::string normalizePlatform(
        const std::string& platform
    );

private:
    Database& m_database;
    dpp::cluster& m_bot;

    mutable std::mutex m_sessionMutex;

    std::unordered_map<
        std::uint64_t,
        UploadSession
    > m_sessions;
};