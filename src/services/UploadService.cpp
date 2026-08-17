#include "services/UploadService.h"

#include "database/Database.h"
#include "discord/ChannelConfig.h"
#include "services/StorageService.h"
#include "services/LatestFeedService.h"
#include "services/AnnouncementService.h"
#include "services/LinkProtectionService.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

namespace
{
    constexpr uint32_t MX_PURPLE = 0x8B5CF6;

    struct StoredUpload
    {
        std::string uploadId;
        std::uint64_t uploaderId = 0;

        std::string scriptName;
        std::string game;
        std::string platform;
        std::string sensitivity;

        int totalParts = 0;
    };

    std::string toLower(const std::string& text)
    {
        std::string result = text;

        std::transform(
            result.begin(),
            result.end(),
            result.begin(),
            [](unsigned char c)
            {
                return static_cast<char>(
                    std::tolower(c)
                    );
            }
        );

        return result;
    }

    std::string makePublicId(long long id)
    {
        std::ostringstream stream;

        stream
            << "MX-"
            << std::setw(6)
            << std::setfill('0')
            << id;

        return stream.str();
    }

    bool loadPendingUpload(
        Database& manager,
        const std::string& uploadId,
        StoredUpload& output
    )
    {
        sqlite3* db = manager.handle();

        if (db == nullptr)
        {
            return false;
        }

        constexpr const char* sql =
            "SELECT "
            "uploader_id, "
            "script_name, "
            "game, "
            "platform, "
            "COALESCE(sensitivity, 'None'), "
            "(SELECT COUNT(*) "
            " FROM upload_parts "
            " WHERE upload_parts.upload_id = uploads.upload_id) "
            "FROM uploads "
            "WHERE upload_id = ? "
            "AND status = 'pending' "
            "LIMIT 1;";

        sqlite3_stmt* statement = nullptr;

        if (
            sqlite3_prepare_v2(
                db,
                sql,
                -1,
                &statement,
                nullptr
            ) != SQLITE_OK
            )
        {
            return false;
        }

        sqlite3_bind_text(
            statement,
            1,
            uploadId.c_str(),
            -1,
            SQLITE_TRANSIENT
        );

        if (sqlite3_step(statement) != SQLITE_ROW)
        {
            sqlite3_finalize(statement);
            return false;
        }

        output.uploadId = uploadId;

        const unsigned char* uploader =
            sqlite3_column_text(statement, 0);

        if (uploader != nullptr)
        {
            try
            {
                output.uploaderId =
                    std::stoull(
                        reinterpret_cast<const char*>(
                            uploader
                            )
                    );
            }
            catch (...)
            {
                output.uploaderId = 0;
            }
        }

        output.scriptName =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 1)
                );

        output.game =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 2)
                );

        output.platform =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 3)
                );

        output.sensitivity =
            reinterpret_cast<const char*>(
                sqlite3_column_text(statement, 4)
                );

        output.totalParts =
            sqlite3_column_int(statement, 5);

        sqlite3_finalize(statement);

        return true;
    }
}

UploadService::UploadService(
    Database& database,
    dpp::cluster& bot
)
    : m_database(database),
    m_bot(bot)
{
    /*
     * Global MX Central systems.
     */

    LinkProtectionService::initialize(
        m_bot
    );


    AnnouncementService::initialize(
        m_bot
    );


    LatestFeedService::initialize(
        m_database,
        m_bot
    );
}

UploadStartResult UploadService::startUpload(
    std::uint64_t userId,
    const std::string& username,
    const std::string& scriptName,
    const std::string& game,
    const std::string& platform,
    const std::string& sensitivity,
    int totalParts
)
{
    UploadStartResult result;

    if (scriptName.empty())
    {
        result.error = "Config name cannot be empty.";
        return result;
    }

    if (game.empty())
    {
        result.error = "Game cannot be empty.";
        return result;
    }

    if (totalParts < 1 || totalParts > 20)
    {
        result.error =
            "Config parts must be between 1 and 20.";

        return result;
    }

    const std::string normalizedPlatform =
        normalizePlatform(platform);

    if (normalizedPlatform.empty())
    {
        result.error =
            "Platform must be PS, Xbox, PC or Not Sure.";

        return result;
    }

    {
        std::lock_guard<std::mutex> lock(
            m_sessionMutex
        );

        if (m_sessions.contains(userId))
        {
            result.error =
                "You already have an active upload.";

            return result;
        }
    }

    if (!ensureUser(userId, username))
    {
        result.error =
            "MX could not create your user record.";

        return result;
    }

    UploadSession session;

    session.uploadId = generateUploadId();
    session.userId = userId;
    session.username = username;
    session.scriptName = scriptName;
    session.game = game;
    session.platform = normalizedPlatform;

    session.sensitivity =
        sensitivity.empty()
        ? "None"
        : sensitivity;

    session.totalParts = totalParts;
    session.nextPart = 1;

    if (!insertUpload(session))
    {
        result.error =
            "MX could not create the upload.";

        return result;
    }

    {
        std::lock_guard<std::mutex> lock(
            m_sessionMutex
        );

        m_sessions[userId] = session;
    }

    result.success = true;
    result.uploadId = session.uploadId;

    return result;
}

UploadPartResult UploadService::capturePart(
    std::uint64_t userId,
    const std::string& configData
)
{
    UploadPartResult result;

    UploadSession completedSession;
    bool completed = false;

    {
        std::lock_guard<std::mutex> lock(
            m_sessionMutex
        );

        const auto iterator =
            m_sessions.find(userId);

        if (iterator == m_sessions.end())
        {
            return result;
        }

        result.handled = true;

        UploadSession& session =
            iterator->second;

        result.uploadId =
            session.uploadId;

        result.totalParts =
            session.totalParts;

        result.partNumber =
            session.nextPart;

        if (
            configData.find_first_not_of(
                " \t\r\n"
            ) == std::string::npos
            )
        {
            result.error =
                "The config part was empty.";

            return result;
        }

        if (
            !insertPart(
                session,
                session.nextPart,
                configData
            )
            )
        {
            result.error =
                "MX failed to save this config part.";

            return result;
        }

        ++session.nextPart;

        result.success = true;

        if (
            session.nextPart >
            session.totalParts
            )
        {
            if (!finalizeUpload(session))
            {
                result.success = false;

                result.error =
                    "The parts were saved but MX "
                    "could not finalize the upload.";

                return result;
            }

            completedSession = session;
            completed = true;
            result.completed = true;

            m_sessions.erase(iterator);
        }
    }

    if (completed)
    {
        incrementUploadStatistics();
        postPendingUpload(completedSession);
    }

    return result;
}

bool UploadService::hasActiveSession(
    std::uint64_t userId
) const
{
    std::lock_guard<std::mutex> lock(
        m_sessionMutex
    );

    return m_sessions.contains(userId);
}

UploadReviewResult UploadService::approveUpload(
    const std::string& uploadId,
    std::uint64_t reviewerId,
    const std::string& reviewerName
)
{
    UploadReviewResult result;
    result.uploadId = uploadId;

    StoredUpload upload;

    if (
        !loadPendingUpload(
            m_database,
            uploadId,
            upload
        )
        )
    {
        result.error =
            "This upload is no longer pending.";

        return result;
    }

    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        result.error = "Database is offline.";
        return result;
    }

    if (
        !m_database.execute(
            "BEGIN IMMEDIATE TRANSACTION;"
        )
        )
    {
        result.error =
            "Could not begin approval transaction.";

        return result;
    }

    constexpr const char* insertSql =
        "INSERT INTO scripts "
        "("
        "source_upload_id, "
        "creator_id, "
        "script_name, "
        "game, "
        "platform, "
        "sensitivity, "
        "verified, "
        "visibility"
        ") "
        "VALUES (?, ?, ?, ?, ?, ?, 1, 'public');";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            insertSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        m_database.execute("ROLLBACK;");
        result.error = "Could not prepare script creation.";
        return result;
    }

    const std::string uploaderId =
        std::to_string(upload.uploaderId);

    sqlite3_bind_text(
        statement,
        1,
        uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        uploaderId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        upload.scriptName.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        4,
        upload.game.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        5,
        upload.platform.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        6,
        upload.sensitivity.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        m_database.execute("ROLLBACK;");

        result.error =
            "Could not create the permanent config.";

        return result;
    }

    sqlite3_finalize(statement);

    const long long scriptId =
        sqlite3_last_insert_rowid(db);

    const std::string publicId =
        makePublicId(scriptId);

    constexpr const char* updateIdSql =
        "UPDATE scripts "
        "SET public_id = ? "
        "WHERE id = ?;";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            updateIdSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        m_database.execute("ROLLBACK;");
        result.error = "Could not assign an MX ID.";
        return result;
    }

    sqlite3_bind_text(
        statement,
        1,
        publicId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int64(
        statement,
        2,
        scriptId
    );

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        m_database.execute("ROLLBACK;");

        result.error = "Could not assign MX ID.";
        return result;
    }

    sqlite3_finalize(statement);

    constexpr const char* copyPartsSql =
        "INSERT INTO script_parts "
        "("
        "script_id, "
        "part_number, "
        "config_data, "
        "config_hash"
        ") "
        "SELECT ?, "
        "part_number, "
        "config_data, "
        "config_hash "
        "FROM upload_parts "
        "WHERE upload_id = ? "
        "ORDER BY part_number ASC;";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            copyPartsSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        m_database.execute("ROLLBACK;");
        result.error = "Could not prepare config storage.";
        return result;
    }

    sqlite3_bind_int64(
        statement,
        1,
        scriptId
    );

    sqlite3_bind_text(
        statement,
        2,
        uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        m_database.execute("ROLLBACK;");

        result.error = "Could not store config parts.";
        return result;
    }

    sqlite3_finalize(statement);

    constexpr const char* approveSql =
        "UPDATE uploads "
        "SET status = 'approved', "
        "reviewer_id = ?, "
        "reviewed_at = CURRENT_TIMESTAMP "
        "WHERE upload_id = ? "
        "AND status = 'pending';";

    statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            approveSql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        m_database.execute("ROLLBACK;");
        result.error = "Could not update upload status.";
        return result;
    }

    const std::string reviewer =
        std::to_string(reviewerId);

    sqlite3_bind_text(
        statement,
        1,
        reviewer.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        m_database.execute("ROLLBACK;");

        result.error = "Approval failed.";
        return result;
    }

    sqlite3_finalize(statement);

    if (!m_database.execute("COMMIT;"))
    {
        m_database.execute("ROLLBACK;");
        result.error = "Could not commit approval.";
        return result;
    }

    m_database.execute(
        "UPDATE statistics "
        "SET value = value + 1, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE key = 'total_scripts';"
    );

    m_database.execute(
        "INSERT INTO daily_statistics "
        "(date, approved_uploads) "
        "VALUES (date('now'), 1) "
        "ON CONFLICT(date) DO UPDATE SET "
        "approved_uploads = approved_uploads + 1;"
    );

    const std::string details =
        "**Config ID:** `" +
        publicId +
        "`\n"
        "**Upload ID:** `" +
        uploadId +
        "`\n"
        "**Config:** `" +
        upload.scriptName +
        "`\n"
        "**Game:** `" +
        upload.game +
        "`\n"
        "**Platform:** `" +
        upload.platform +
        "`\n"
        "**Sensitivity:** `" +
        upload.sensitivity +
        "`\n"
        "**Parts:** `" +
        std::to_string(upload.totalParts) +
        "`\n"
        "**Uploader:** <@" +
        std::to_string(upload.uploaderId) +
        ">\n"
        "**Approved By:** `" +
        reviewerName +
        "`";

    StorageService::log(
        m_bot,
        MXChannels::Storage::APPROVED_UPLOADS,
        "✅ Config Approved",
        details
    );

    StorageService::log(
        m_bot,
        MXChannels::Storage::SCRIPT_STORAGE,
        "📦 Config Stored",
        "**MX ID:** `" +
        publicId +
        "`\n"
        "**Config:** `" +
        upload.scriptName +
        "`\n"
        "**Parts:** `" +
        std::to_string(upload.totalParts) +
        "`\n"
        "**Storage:** `mx_central.db`"
    );

    StorageService::log(
        m_bot,
        MXChannels::Storage::SCRIPT_METADATA,
        "📑 Script Metadata",
        details
    );

    StorageService::logUpload(
        m_bot,
        reviewerName,
        upload.scriptName,
        "Approved as " + publicId
    );

    /*
     * Refresh the single persistent latest-releases panel.
     *
     * No new public release message is created here.
     */
    LatestFeedService::refresh();

    result.success = true;
    result.publicId = publicId;
    result.scriptName = upload.scriptName;
    result.game = upload.game;
    result.platform = upload.platform;
    result.sensitivity = upload.sensitivity;
    result.uploaderId = upload.uploaderId;
    result.totalParts = upload.totalParts;

    return result;
}

UploadReviewResult UploadService::rejectUpload(
    const std::string& uploadId,
    std::uint64_t reviewerId,
    const std::string& reviewerName,
    const std::string& reason
)
{
    UploadReviewResult result;
    result.uploadId = uploadId;

    StoredUpload upload;

    if (
        !loadPendingUpload(
            m_database,
            uploadId,
            upload
        )
        )
    {
        result.error =
            "This upload is no longer pending.";

        return result;
    }

    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        result.error = "Database is offline.";
        return result;
    }

    constexpr const char* sql =
        "UPDATE uploads "
        "SET status = 'rejected', "
        "rejection_reason = ?, "
        "reviewer_id = ?, "
        "reviewed_at = CURRENT_TIMESTAMP "
        "WHERE upload_id = ? "
        "AND status = 'pending';";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        result.error =
            "Could not prepare rejection.";

        return result;
    }

    const std::string reviewer =
        std::to_string(reviewerId);

    sqlite3_bind_text(
        statement,
        1,
        reason.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        reviewer.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        3,
        uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    if (!success)
    {
        result.error = "Rejection failed.";
        return result;
    }

    m_database.execute(
        "INSERT INTO daily_statistics "
        "(date, rejected_uploads) "
        "VALUES (date('now'), 1) "
        "ON CONFLICT(date) DO UPDATE SET "
        "rejected_uploads = rejected_uploads + 1;"
    );

    StorageService::log(
        m_bot,
        MXChannels::Storage::REJECTED_UPLOADS,
        "❌ Config Rejected",
        "**Upload ID:** `" +
        uploadId +
        "`\n"
        "**Config:** `" +
        upload.scriptName +
        "`\n"
        "**Game:** `" +
        upload.game +
        "`\n"
        "**Platform:** `" +
        upload.platform +
        "`\n"
        "**Uploader:** <@" +
        std::to_string(upload.uploaderId) +
        ">\n"
        "**Rejected By:** `" +
        reviewerName +
        "`\n"
        "**Reason:** `" +
        reason +
        "`"
    );

    StorageService::logUpload(
        m_bot,
        reviewerName,
        upload.scriptName,
        "Rejected"
    );

    result.success = true;
    result.scriptName = upload.scriptName;
    result.game = upload.game;
    result.platform = upload.platform;
    result.sensitivity = upload.sensitivity;
    result.uploaderId = upload.uploaderId;
    result.totalParts = upload.totalParts;

    return result;
}

bool UploadService::ensureUser(
    std::uint64_t userId,
    const std::string& username
)
{
    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "INSERT INTO users "
        "(discord_id, username) "
        "VALUES (?, ?) "
        "ON CONFLICT(discord_id) DO UPDATE SET "
        "username = excluded.username, "
        "last_seen_at = CURRENT_TIMESTAMP;";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }

    const std::string discordId =
        std::to_string(userId);

    sqlite3_bind_text(
        statement,
        1,
        discordId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        username.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

bool UploadService::insertUpload(
    const UploadSession& session
)
{
    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "INSERT INTO uploads "
        "(upload_id, uploader_id, script_name, "
        "game, platform, sensitivity, status) "
        "VALUES (?, ?, ?, ?, ?, ?, 'collecting');";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }

    const std::string userId =
        std::to_string(session.userId);

    sqlite3_bind_text(statement, 1, session.uploadId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, session.scriptName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, session.game.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, session.platform.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 6, session.sensitivity.c_str(), -1, SQLITE_TRANSIENT);

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

bool UploadService::insertPart(
    const UploadSession& session,
    int partNumber,
    const std::string& configData
)
{
    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "INSERT INTO upload_parts "
        "(upload_id, part_number, config_data) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }

    sqlite3_bind_text(
        statement,
        1,
        session.uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(
        statement,
        2,
        partNumber
    );

    sqlite3_bind_text(
        statement,
        3,
        configData.c_str(),
        static_cast<int>(configData.size()),
        SQLITE_TRANSIENT
    );

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

bool UploadService::finalizeUpload(
    const UploadSession& session
)
{
    sqlite3* db = m_database.handle();

    if (db == nullptr)
    {
        return false;
    }

    constexpr const char* sql =
        "UPDATE uploads "
        "SET status = 'pending' "
        "WHERE upload_id = ?;";

    sqlite3_stmt* statement = nullptr;

    if (
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
        )
    {
        return false;
    }

    sqlite3_bind_text(
        statement,
        1,
        session.uploadId.c_str(),
        -1,
        SQLITE_TRANSIENT
    );

    const bool success =
        sqlite3_step(statement) == SQLITE_DONE;

    sqlite3_finalize(statement);

    return success;
}

void UploadService::postPendingUpload(
    const UploadSession& session
)
{
    dpp::embed embed;

    embed
        .set_color(MX_PURPLE)
        .set_title("🟡 Config Awaiting Approval")
        .set_description(
            "**Upload ID:** `" +
            session.uploadId +
            "`\n"
            "**Uploader:** <@" +
            std::to_string(session.userId) +
            ">\n"
            "**Config:** `" +
            session.scriptName +
            "`\n"
            "**Game:** `" +
            session.game +
            "`\n"
            "**Platform:** `" +
            session.platform +
            "`\n"
            "**Sensitivity:** `" +
            session.sensitivity +
            "`\n"
            "**Parts:** `" +
            std::to_string(session.totalParts) +
            "`\n"
            "**Status:** 🟡 Pending Approval"
        )
        .set_footer(
            dpp::embed_footer()
            .set_text(
                "MX Central • Upload Review"
            )
        );

    dpp::component row;

    row.add_component(
        dpp::component()
        .set_label("Approve")
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_success)
        .set_id(
            "mx_approve:" +
            session.uploadId
        )
    );

    row.add_component(
        dpp::component()
        .set_label("Reject")
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_id(
            "mx_reject:" +
            session.uploadId
        )
    );

    dpp::message message;

    message.set_channel_id(
        MXChannels::Storage::PENDING_UPLOADS
    );

    message.add_embed(embed);
    message.add_component(row);

    m_bot.message_create(message);

    StorageService::logUpload(
        m_bot,
        session.username,
        session.scriptName,
        "Submitted for approval"
    );

    StorageService::log(
        m_bot,
        MXChannels::Storage::SCRIPT_METADATA,
        "📑 Pending Script Metadata",
        "**Upload ID:** `" +
        session.uploadId +
        "`\n"
        "**Config:** `" +
        session.scriptName +
        "`\n"
        "**Game:** `" +
        session.game +
        "`\n"
        "**Platform:** `" +
        session.platform +
        "`"
    );
}

void UploadService::incrementUploadStatistics()
{
    m_database.execute(
        "UPDATE statistics "
        "SET value = value + 1, "
        "updated_at = CURRENT_TIMESTAMP "
        "WHERE key = 'total_uploads';"
    );

    m_database.execute(
        "INSERT INTO daily_statistics "
        "(date, uploads) "
        "VALUES (date('now'), 1) "
        "ON CONFLICT(date) DO UPDATE SET "
        "uploads = uploads + 1;"
    );
}

std::string UploadService::generateUploadId()
{
    const auto milliseconds =
        std::chrono::duration_cast<
        std::chrono::milliseconds
        >(
            std::chrono::system_clock::now()
            .time_since_epoch()
        ).count();

    static std::mt19937 generator(
        std::random_device{}()
    );

    std::uniform_int_distribution<int>
        randomNumber(1000, 9999);

    return
        "UP-" +
        std::to_string(milliseconds) +
        "-" +
        std::to_string(
            randomNumber(generator)
        );
}

std::string UploadService::normalizePlatform(
    const std::string& platform
)
{
    const std::string value =
        toLower(platform);

    if (
        value == "ps" ||
        value == "ps5" ||
        value == "playstation" ||
        value == "playstation 5"
        )
    {
        return "PS";
    }

    if (
        value == "xbox" ||
        value == "xb" ||
        value == "xbox one" ||
        value == "xbox series" ||
        value == "xbox series x" ||
        value == "xbox series s"
        )
    {
        return "Xbox";
    }

    if (
        value == "pc" ||
        value == "windows" ||
        value == "computer"
        )
    {
        return "PC";
    }

    if (
        value == "not sure" ||
        value == "notsure" ||
        value == "unsure" ||
        value == "unknown" ||
        value == "idk" ||
        value == "dont know" ||
        value == "don't know" ||
        value == "n/a" ||
        value == "na"
        )
    {
        return "Not Sure";
    }

    return "";
}