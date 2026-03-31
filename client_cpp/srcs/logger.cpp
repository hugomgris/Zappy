#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>


namespace {
LogLevel g_minLevel = LogLevel::Info;
std::mutex g_logMutex;

const char *toString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}
}  // namespace

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_minLevel = level;
}

void Logger::debug(const std::string &message) { log(LogLevel::Debug, message); }
void Logger::info(const std::string &message) { log(LogLevel::Info, message); }
void Logger::warn(const std::string &message) { log(LogLevel::Warn, message); }
void Logger::error(const std::string &message) { log(LogLevel::Error, message); }

void Logger::log(LogLevel level, const std::string &message) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    if (static_cast<int>(level) < static_cast<int>(g_minLevel)) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::cerr << "[" << std::put_time(std::localtime(&nowTime), "%F %T") << "] "
              << "[" << toString(level) << "] " << message << std::endl;
}
