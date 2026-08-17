#pragma once

#include <dpp/dpp.h>

class UploadService;

namespace ReviewCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        UploadService& uploadService
    );
}