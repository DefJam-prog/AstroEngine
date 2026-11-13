#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace astro::core
{
enum class LogLevel { Trace, Debug, Info, Warning, Error, Off };
using LogSink = std::function<void(LogLevel, std::string_view)>;

class Logger
{
public:
    explicit Logger(LogSink sink = {}, LogLevel threshold = LogLevel::Info);
    void setSink(LogSink sink);
    void setThreshold(LogLevel threshold) noexcept;
    [[nodiscard]] LogLevel threshold() const noexcept;
    void write(LogLevel level, std::string_view message) const;
    void trace(std::string_view message) const { write(LogLevel::Trace, message); }
    void debug(std::string_view message) const { write(LogLevel::Debug, message); }
    void info(std::string_view message) const { write(LogLevel::Info, message); }
    void warning(std::string_view message) const { write(LogLevel::Warning, message); }
    void error(std::string_view message) const { write(LogLevel::Error, message); }

private:
    mutable std::mutex mutex_;
    LogSink sink_;
    LogLevel threshold_;
};
}
