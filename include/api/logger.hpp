#pragma once

#include <deque>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>

namespace api
{

struct server_config;

enum class severity_level
{
    trace,
    debug,
    info,
    warning,
    error,
    fatal
};

std::ostream& operator<<(std::ostream& os, severity_level lvl);
severity_level string_to_severity(const std::string& s);

namespace detail
{

constexpr const char* file_basename(const char* path)
{
    const char* file = path;

    while (*path)
    {
        if (*path == '/' || *path == '\\')
        {
            file = path + 1;
        }

        path++;
    }

    return file;
}

struct log_stream
{
    std::ostringstream oss;

    template <typename T>
    log_stream& operator<<(const T& val)
    {
        oss << val;

        return *this;
    }

    log_stream& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        oss << manip;

        return *this;
    }

    log_stream& operator<<(std::ios_base& (*manip)(std::ios_base&))
    {
        oss << manip;

        return *this;
    }

    std::string str() const
    {
        return oss.str();
    }
};

} // namespace detail

struct log_entry
{
    std::string timestamp;
    severity_level level = severity_level::info;
    std::string channel;
    std::string file;
    int line = 0;
    std::string function;
    std::string message;
};

class log_ring_buffer
{
public:
    explicit log_ring_buffer(size_t capacity = 1000);
    void push(log_entry entry);
    std::vector<log_entry> snapshot() const;
    std::vector<log_entry> snapshot_errors() const;
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::deque<log_entry> buffer_;
    size_t capacity_;
};

void init_logging(const server_config& cfg);
log_ring_buffer& get_log_ring_buffer();
boost::log::sources::severity_channel_logger_mt<severity_level, std::string>& get_logger();

void log_message(const std::string& channel, severity_level lvl,
                 const char* file, int line, const char* func,
                 const std::string& msg);

} // namespace api

#define LOG_TRACE(channel, msg)                                                \
    ::api::log_message(channel, ::api::severity_level::trace,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())

#define LOG_DEBUG(channel, msg)                                                \
    ::api::log_message(channel, ::api::severity_level::debug,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())

#define LOG_INFO(channel, msg)                                                 \
    ::api::log_message(channel, ::api::severity_level::info,                   \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())

#define LOG_WARN(channel, msg)                                                 \
    ::api::log_message(channel, ::api::severity_level::warning,                \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())

#define LOG_ERROR(channel, msg)                                                \
    ::api::log_message(channel, ::api::severity_level::error,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())

#define LOG_FATAL(channel, msg)                                                \
    ::api::log_message(channel, ::api::severity_level::fatal,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,        \
                       __func__, (::api::detail::log_stream() << msg).str())
