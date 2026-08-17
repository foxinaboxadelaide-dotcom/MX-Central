#include "utils/Logger.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {

    std::string getTimestamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);

        std::tm localTime{};

#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        std::ostringstream stream;

        stream << std::put_time(
            &localTime,
            "%Y-%m-%d %H:%M:%S"
        );

        return stream.str();
    }

    const char* levelName(Logger::Level level) {
        switch (level) {
        case Logger::Level::INFO:
            return "INFO";

        case Logger::Level::WARNING:
            return "WARNING";

        case Logger::Level::ERROR:
            return "ERROR";

        case Logger::Level::DEBUG:
            return "DEBUG";

        case Logger::Level::SUCCESS:
            return "SUCCESS";
        }

        return "UNKNOWN";
    }
}

namespace Logger {

    void initialize() {
        std::cout << "Logger initialized." << std::endl;
    }

    void log(Level level, const std::string& message) {
        std::cout
            << "["
            << getTimestamp()
            << "] "
            << levelName(level)
            << ": "
            << message
            << std::endl;
    }

    void info(const std::string& message) {
        log(Level::INFO, message);
    }

    void warning(const std::string& message) {
        log(Level::WARNING, message);
    }

    void error(const std::string& message) {
        log(Level::ERROR, message);
    }

    void debug(const std::string& message) {
        log(Level::DEBUG, message);
    }

    void success(const std::string& message) {
        log(Level::SUCCESS, message);
    }
}