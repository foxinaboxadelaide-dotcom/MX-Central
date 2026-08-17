#pragma once

#include <cstdint>
#include <string>

struct UploadSession
{
    std::string uploadId;

    std::uint64_t userId = 0;

    std::string username;

    std::string scriptName;

    std::string game;

    std::string platform;

    std::string sensitivity;

    int totalParts = 1;

    int nextPart = 1;
};