#include "bot/Bot.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main()
{
    std::cout << "==========================" << std::endl;
    std::cout << "        MX CENTRAL        " << std::endl;
    std::cout << "==========================" << std::endl;

    /*
     * ============================================================
     * DISCORD TOKEN
     * ============================================================
     *
     * The token is no longer stored in the source code.
     *
     * Local Windows:
     *   Set DISCORD_TOKEN as an environment variable.
     *
     * Railway:
     *   Add DISCORD_TOKEN in the Railway Variables tab.
     */

    const char* tokenEnvironment =
        std::getenv("DISCORD_TOKEN");

    if (
        tokenEnvironment == nullptr ||
        std::string(tokenEnvironment).empty()
        )
    {
        std::cerr
            << "Fatal error: DISCORD_TOKEN environment variable is not set."
            << std::endl;

        return 1;
    }

    const std::string token =
        tokenEnvironment;

    std::cout
        << "MX Central starting..."
        << std::endl;

    std::cout
        << "Connecting to Discord..."
        << std::endl;

    try
    {
        Bot bot(token);

        bot.start();
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "Fatal error: "
            << e.what()
            << std::endl;

        return 1;
    }
    catch (...)
    {
        std::cerr
            << "Fatal error: Unknown exception."
            << std::endl;

        return 1;
    }

    return 0;
}