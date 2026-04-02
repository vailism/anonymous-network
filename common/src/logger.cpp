#include "common/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace anet {
namespace {

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
#if defined(__APPLE__) || defined(__linux__)
    localtime_r(&currentTime, &localTm);
#else
    localtime_s(&localTm, &currentTime);
#endif

    std::ostringstream output;
    output << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

}  // namespace

Logger::Logger(std::string component)
    : component_(std::move(component)) {}

void Logger::info(const std::string& message) const {
    log("INFO", message);
}

void Logger::warn(const std::string& message) const {
    log("WARN", message);
}

void Logger::error(const std::string& message) const {
    log("ERROR", message);
}

void Logger::log(const char* level, const std::string& message) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << '[' << currentTimestamp() << "] [" << level << "] [" << component_ << "] " << message << '\n';
}

}  // namespace anet
