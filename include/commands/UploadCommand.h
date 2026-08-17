#pragma once

#include <dpp/dpp.h>

class UploadService;

namespace UploadCommand
{
    void registerHandlers(
        dpp::cluster& bot,
        UploadService& uploadService
    );

    void openUploadModal(
        const dpp::slashcommand_t& event
    );
}