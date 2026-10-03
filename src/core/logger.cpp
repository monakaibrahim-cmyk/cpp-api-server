#include "api/logger.h"
#include "api/config.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

#include <boost/core/null_deleter.hpp>
#include <boost/log/attributes/clock.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/make_shared.hpp>
#include <boost/shared_ptr.hpp>

namespace api
{

std::ostream &operator<<(std::ostream &output_stream, severity_level level)
{
    static const char *labels[] = {"TRACE", "DEBUG", "INFO ",
                                   "WARN ", "ERROR", "FATAL"};

    auto index = static_cast<int>(level);

    if (index >= 0 && index < 6)
    {
        output_stream << labels[index];
    }
    else
    {
        output_stream << "???  ";
    }

    return output_stream;
}

severity_level string_to_severity(const std::string &severity_string)
{
    if (severity_string == "trace")
    {
        return severity_level::trace;
    }

    if (severity_string == "debug")
    {
        return severity_level::debug;
    }

    if (severity_string == "info")
    {
        return severity_level::info;
    }

    if (severity_string == "warning" || severity_string == "warn")
    {
        return severity_level::warning;
    }

    if (severity_string == "error")
    {
        return severity_level::error;
    }

    if (severity_string == "fatal")
    {
        return severity_level::fatal;
    }

    return severity_level::info;
}

log_ring_buffer::log_ring_buffer(size_t capacity) : capacity_(capacity) {}

void log_ring_buffer::push(LogEntry entry)
{
    std::lock_guard<std::mutex> lock(mutex_);

    buffer_.push_back(std::move(entry));

    while (buffer_.size() > capacity_)
    {
        buffer_.pop_front();
    }
}

std::vector<LogEntry> log_ring_buffer::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return {buffer_.begin(), buffer_.end()};
}

std::vector<LogEntry> log_ring_buffer::snapshot_errors() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> errors;

    for (const auto &entry : buffer_)
    {
        if (entry.level >= severity_level::warning)
        {
            errors.push_back(entry);
        }
    }

    return errors;
}

size_t log_ring_buffer::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return buffer_.size();
}

static log_ring_buffer g_ring_buffer(1000);
static boost::log::sources::severity_channel_logger_mt<severity_level,
                                                       std::string>
    g_logger(boost::log::keywords::channel = "server");

log_ring_buffer &get_log_ring_buffer() { return g_ring_buffer; }

boost::log::sources::severity_channel_logger_mt<severity_level, std::string> &
get_logger()
{
    return g_logger;
}

BOOST_LOG_ATTRIBUTE_KEYWORD(severity_attr, "Severity", severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(channel_attr, "Channel", std::string)

void log_message(const std::string &channel, severity_level level,
                 const char *file, int line, const char *function_name,
                 const std::string &message)
{
    auto time_point = std::chrono::system_clock::now();
    auto time_t_value = std::chrono::system_clock::to_time_t(time_point);
    auto milliseconds_remainder =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time_point.time_since_epoch()) %
        1000;
    std::tm time_structure{};
    char time_buffer[32];

    localtime_r(&time_t_value, &time_structure);
    std::snprintf(time_buffer, sizeof(time_buffer), "%02d:%02d:%02d.%03d",
                  time_structure.tm_hour, time_structure.tm_min,
                  time_structure.tm_sec,
                  static_cast<int>(milliseconds_remainder.count()));

    LogEntry entry;

    entry.timestamp = time_buffer;
    entry.level = level;
    entry.channel = channel;
    entry.file = file ? file : "";
    entry.line = line;
    entry.function = function_name ? function_name : "";
    entry.message = message;

    g_ring_buffer.push(std::move(entry));

    BOOST_LOG_CHANNEL_SEV(g_logger, channel, level)
        << "[" << (file ? file : "unknown") << ":" << line << " "
        << (function_name ? function_name : "unknown") << "] " << message;
}

void init_logging(const ServerConfig &configuration)
{
    namespace logging = boost::log;
    namespace sinks = boost::log::sinks;
    namespace expr = boost::log::expressions;

    auto core = logging::core::get();

    logging::add_common_attributes();

    auto minimum_severity = string_to_severity(configuration.log_level);

    core->set_filter(severity_attr >= minimum_severity);

    {
        auto backend = boost::make_shared<sinks::text_ostream_backend>();

        backend->add_stream(
            boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));
        backend->auto_flush(true);

        using sink_type = sinks::synchronous_sink<sinks::text_ostream_backend>;
        auto sink = boost::make_shared<sink_type>(backend);

        sink->set_formatter(expr::stream
                            << "["
                            << expr::format_date_time<boost::posix_time::ptime>(
                                   "TimeStamp", "%Y-%m-%d %H:%M:%S")
                            << "] [" << severity_attr << "] [" << channel_attr
                            << "] " << expr::smessage);

        core->add_sink(sink);
    }

    {
        namespace fs = std::filesystem;

        if (!fs::exists(configuration.log_dir))
        {
            fs::create_directories(configuration.log_dir);
        }

        auto backend = boost::make_shared<sinks::text_file_backend>(
            boost::log::keywords::file_name =
                configuration.log_dir + "/api_%Y%m%d_%H%M%S.log",
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::auto_flush = true);

        using sink_type = sinks::synchronous_sink<sinks::text_file_backend>;
        auto sink = boost::make_shared<sink_type>(backend);

        sink->set_formatter(expr::stream
                            << "["
                            << expr::format_date_time<boost::posix_time::ptime>(
                                   "TimeStamp", "%Y-%m-%d %H:%M:%S")
                            << "] [" << severity_attr << "] [" << channel_attr
                            << "] " << expr::smessage);

        core->add_sink(sink);
    }
}

} // namespace api
