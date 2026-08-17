#pragma once

#include <dpp/dpp.h>

namespace MXChannels
{
    /*
     * ============================================================
     * SERVERS
     * ============================================================
     */

    constexpr dpp::snowflake MAIN_SERVER_ID =
        1535283967031378010ULL;

    constexpr dpp::snowflake STORAGE_SERVER_ID =
        1536296235940581426ULL;


    /*
     * ============================================================
     * MAIN MX CENTRAL SERVER
     * ============================================================
     */

    namespace Main
    {
        /*
         * WELCOME
         */

        constexpr dpp::snowflake RULES =
            1536289259193892904ULL;

        constexpr dpp::snowflake ANNOUNCEMENTS =
            1536289485959077898ULL;

        constexpr dpp::snowflake WELCOME =
            1536289531085455432ULL;


        /*
         * CENTRAL
         */

        constexpr dpp::snowflake FIND_CONFIG =
            1536289654452785152ULL;

        constexpr dpp::snowflake LATEST_RELEASES =
            1536289708328484924ULL;

        constexpr dpp::snowflake TRENDING_CONFIGS =
            1536289757607239701ULL;


        /*
         * COMMUNITY
         */

        constexpr dpp::snowflake SUGGESTIONS =
            1536290416587182100ULL;

        constexpr dpp::snowflake GENERAL =
            1536290295874977837ULL;

        constexpr dpp::snowflake CLIPS =
            1536290475361701908ULL;


        /*
         * TOOLS
         */

        constexpr dpp::snowflake UPLOAD_CONFIG =
            1536290670292238376ULL;

        /*
         * Existing ticket/help channel.
         * MX help centre does NOT use this.
         */
        constexpr dpp::snowflake TICKETS =
            1536290717431898192ULL;

        /*
         * New dedicated MX Help Centre.
         */
        constexpr dpp::snowflake HELP =
            1538833092410740827ULL;


        /*
         * INFORMATION
         */

        constexpr dpp::snowflake STATISTICS =
            1536290887317979187ULL;

        constexpr dpp::snowflake BOT_STATUS =
            1538092374864035882ULL;
    }


    /*
     * ============================================================
     * PRIVATE MX BOT STORAGE SERVER
     * ============================================================
     */

    namespace Storage
    {
        /*
         * CONTROL
         */

        constexpr dpp::snowflake BOT_CONSOLE =
            1536296590506065991ULL;

        constexpr dpp::snowflake CLOUD_STATUS =
            1536296644134310001ULL;

        constexpr dpp::snowflake SECURITY_EVENTS =
            1538080965627486309ULL;

        constexpr dpp::snowflake SYSTEM_CONFIG =
            1538081007591493742ULL;


        /*
         * UPLOADS
         */

        constexpr dpp::snowflake PENDING_UPLOADS =
            1536296765509083246ULL;

        constexpr dpp::snowflake APPROVED_UPLOADS =
            1536296823155720212ULL;

        constexpr dpp::snowflake REJECTED_UPLOADS =
            1536296869355986965ULL;


        /*
         * SCRIPT STORAGE
         */

        constexpr dpp::snowflake SCRIPT_STORAGE =
            1536297007914950728ULL;

        constexpr dpp::snowflake SCRIPT_METADATA =
            1536297061702438943ULL;


        /*
         * LOGS
         */

        constexpr dpp::snowflake UPLOAD_LOGS =
            1536297482609360976ULL;

        constexpr dpp::snowflake DOWNLOAD_LOGS =
            1536297544160911361ULL;

        constexpr dpp::snowflake RATING_LOGS =
            1536297597369720903ULL;

        constexpr dpp::snowflake SYSTEM_LOGS =
            1536297650591244339ULL;

        constexpr dpp::snowflake ERROR_LOGS =
            1536297715128860774ULL;

        constexpr dpp::snowflake COMMAND_LOGS =
            1538080706096402452ULL;


        /*
         * BACKUPS
         */

        constexpr dpp::snowflake DATABASE_BACKUPS =
            1536297824520642640ULL;

        constexpr dpp::snowflake CLOUD_BACKUPS =
            1536297925548707850ULL;

        constexpr dpp::snowflake SCRIPT_BACKUPS =
            1538081223140966502ULL;

        constexpr dpp::snowflake BACKUP_LOGS =
            1538081284092334111ULL;
    }
}