#include "logger.hpp"
#include "result.hpp"

#include <iostream>

Result bootstrap() {
    Logger::setLevel(LogLevel::Debug);
    Logger::info("C++ client bootstrap initialized");
    return Result::success();
}

int main() {
    const Result res = bootstrap();
    if (!res.ok()) {
        Logger::error("Bootstrap failed: " + res.message);
        return static_cast<int>(res.code);
    }

    std::cout << "client_cpp bootstrap ok" << std::endl;
    return 0;
}