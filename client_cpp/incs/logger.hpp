#pragma once

#include <string>

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void setLevel(LogLevel level);
    static void debug(const std::string &message);
    static void info(const std::string &message);
    static void warn(const std::string &message);
    static void error(const std::string &message);

private:
    static void log(LogLevel level, const std::string &message);
};
