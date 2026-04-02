#pragma once

#include <mutex>
#include <string>

namespace anet {

class Logger {
public:
    explicit Logger(std::string component);

    void info(const std::string& message) const;
    void warn(const std::string& message) const;
    void error(const std::string& message) const;

private:
    void log(const char* level, const std::string& message) const;

    std::string component_;
    mutable std::mutex mutex_;
};

}  // namespace anet
