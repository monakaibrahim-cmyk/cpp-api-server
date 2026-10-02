#include "api/logger.hpp"
#include "api/config.hpp"

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

std::ostream& operator<<(std::ostream& os, severity_level lvl)
{
    static const char* labels[] = {
        "TRACE",
        "DEBUG",
        "INFO ",
        "WARN ",
        "ERROR",
        "FATAL"
    };

    auto idx = static_cast<int>(lvl);
    
    if (idx >= 0 && idx < 6)
    {
        os << labels[idx];
    }
    else
    {
        os << "???  ";
    }

    return os;
}

severity_level string_to_severity(const std::string& s)
{
    if (s == "trace")
    {
        return severity_level::trace;
    }

    if (s == "debug")
    {
        return severity_level::debug;
    }

    if (s == "info")
    {
        return severity_level::info;
    }

    if (s == "warning" || s == "warn")
    {
        return severity_level::warning;
    }

    if (s == "error")
    {
        return severity_level::error;
    }

    if (s == "fatal")
    {
        return severity_level::fatal;
    }

    return severity_level::info;
}

log_ring_buffer::log_ring_buffer(size_t capacity)
    : capacity_(capacity)
{
}

void log_ring_buffer::push(log_entry entry)
{
    std::lock_guard<std::mutex> lk(mutex_);

    buffer_.push_back(std::move(entry));

    while (buffer_.size() > capacity_)
    {
        buffer_.pop_front();
    }
}

std::vector<log_entry> log_ring_buffer::snapshot() const
{
    std::lock_guard<std::mutex> lk(mutex_);

    return {buffer_.begin(), buffer_.end()};
}

std::vector<log_entry> log_ring_buffer::snapshot_errors() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<log_entry> errors;

    for (const auto& entry : buffer_)
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
    std::lock_guard<std::mutex> lk(mutex_);

    return buffer_.size();
}

static log_ring_buffer g_ring_buffer(1000);
static boost::log::sources::severity_channel_logger_mt<severity_level, std::string> g_logger(boost::log::keywords::channel = "server");

log_ring_buffer& get_log_ring_buffer()
{
    return g_ring_buffer;
}

boost::log::sources::severity_channel_logger_mt<severity_level, std::string>& get_logger()
{
    return g_logger;
}

BOOST_LOG_ATTRIBUTE_KEYWORD(severity_attr, "Severity", severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(channel_attr, "Channel", std::string)

void log_message(
    const std::string& channel,
    severity_level lvl,
    const char* file,
    int line,
    const char* func,
    const std::string& msg
)
{
    auto tp = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    char tbuf[32];

    localtime_r(&tt, &tm_buf);
    std::snprintf(
        tbuf,
        sizeof(tbuf),
        "%02d:%02d:%02d.%03d",
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec,
        static_cast<int>(ms.count())
    );

    log_entry entry;

    entry.timestamp = tbuf;
    entry.level = lvl;
    entry.channel = channel;
    entry.file = file ? file : "";
    entry.line = line;
    entry.function = func ? func : "";
    entry.message = msg;

    g_ring_buffer.push(std::move(entry));

    BOOST_LOG_CHANNEL_SEV(g_logger, channel, lvl)
        << "[" << (file ? file : "unknown") << ":" << line << " " << (func ? func : "unknown") << "] "
        << msg;
}

void init_logging(const server_config& cfg)
{
    namespace logging = boost::log;
    namespace sinks = boost::log::sinks;
    namespace expr = boost::log::expressions;

    auto core = logging::core::get();

    logging::add_common_attributes();

    auto min_sev = string_to_severity(cfg.log_level);

    core->set_filter(severity_attr >= min_sev);

    {
        auto backend = boost::make_shared<sinks::text_ostream_backend>();

        backend->add_stream(boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter()));
        backend->auto_flush(true);

        using sink_t = sinks::synchronous_sink<sinks::text_ostream_backend>;
        auto sink = boost::make_shared<sink_t>(backend);

        sink->set_formatter(
            expr::stream
            << "[" << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
            << "] [" << severity_attr << "] ["
            << channel_attr << "] "
            << expr::smessage
        );

        core->add_sink(sink);
    }

    {
        namespace fs = std::filesystem;

        if (!fs::exists(cfg.log_dir))
        {
            fs::create_directories(cfg.log_dir);
        }

        auto backend = boost::make_shared<sinks::text_file_backend>(
            boost::log::keywords::file_name = cfg.log_dir + "/api_%Y%m%d_%H%M%S.log",
            boost::log::keywords::rotation_size = 10 * 1024 * 1024,
            boost::log::keywords::auto_flush = true
        );

        using sink_t = sinks::synchronous_sink<sinks::text_file_backend>;
        auto sink = boost::make_shared<sink_t>(backend);

        sink->set_formatter(
            expr::stream
            << "[" << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
            << "] [" << severity_attr << "] ["
            << channel_attr << "] "
            << expr::smessage
        );

        core->add_sink(sink);
    }
}

} // namespace api
