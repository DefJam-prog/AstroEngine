#include "astro/core/Logger.hpp"

#include <iostream>
#include <utility>

namespace astro::core
{
namespace
{
std::string_view label(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    case LogLevel::Off: return "off";
    }
    return "unknown";
}

void defaultSink(LogLevel level, std::string_view message)
{
    auto& stream = level >= LogLevel::Warning ? std::cerr : std::clog;
    stream << '[' << label(level) << "] " << message << '\n';
}
}

Logger::Logger(LogSink sink, LogLevel threshold) : sink_(sink ? std::move(sink) : defaultSink), threshold_(threshold) {}

void Logger::setSink(LogSink sink)
{
    std::scoped_lock lock(mutex_);
    sink_ = sink ? std::move(sink) : defaultSink;
}

void Logger::setThreshold(LogLevel threshold) noexcept
{
    std::scoped_lock lock(mutex_);
    threshold_ = threshold;
}

LogLevel Logger::threshold() const noexcept
{
    std::scoped_lock lock(mutex_);
    return threshold_;
}

void Logger::write(LogLevel level, std::string_view message) const
{
    LogSink sink;
    {
        std::scoped_lock lock(mutex_);
        if (threshold_ == LogLevel::Off || level < threshold_ || level == LogLevel::Off) return;
        sink = sink_;
    }
    sink(level, message);
}
}
