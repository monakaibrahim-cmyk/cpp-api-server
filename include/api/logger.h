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

struct ServerConfig;

/**
 * @brief Severity levels for log classification, filtering, and terminal
 * formatting.
 */
enum class severity_level
{
    /** @brief Ultra-verbose diagnostics tracing execution control flow and
       buffers. */
    trace,

    /** @brief Diagnostic information valuable during development and
       troubleshooting. */
    debug,

    /** @brief General operational milestones (e.g. startup, request completion,
       module loads). */
    info,

    /** @brief Warning conditions that do not halt operation but indicate
       potential issues. */
    warning,

    /** @brief Recoverable runtime errors (e.g. failed queries, missing routes,
       network timeouts). */
    error,

    /** @brief Catastrophic failures requiring immediate process shutdown. */
    fatal
};

/**
 * @brief Serializes a @ref severity_level enum value to an output stream as an
 * uppercase label.
 *
 * @param[in,out] output_stream Destination stream.
 * @param[in] level Severity level enum value.
 * @return std::ostream& Reference to output_stream.
 */
std::ostream &operator<<(std::ostream &output_stream, severity_level level);

/**
 * @brief Parses a textual log level name into a @ref severity_level enum value.
 *
 * @param[in] severity_string Case-sensitive name (e.g. "trace", "debug",
 * "info", "warn", "warning", "error", "fatal").
 * @return severity_level Corresponding enum value (defaults to
 * severity_level::info on unknown input).
 */
severity_level string_to_severity(const std::string &severity_string);

namespace detail
{

/**
 * @brief Extracts the file basename from a full path at compile time.
 *
 * @param[in] file_path Path string (typically __FILE__).
 * @return const char* Pointer to the beginning of the filename component.
 */
constexpr const char *file_basename(const char *file_path)
{
    const char *filename = file_path;

    while (*file_path)
    {
        if (*file_path == '/' || *file_path == '\\')
        {
            filename = file_path + 1;
        }

        file_path++;
    }

    return filename;
}

/**
 * @brief Helper stream accumulating message parts for logger macros.
 */
struct LogStream
{
    std::ostringstream string_stream;

    template <typename T> LogStream &operator<<(const T &value)
    {
        string_stream << value;

        return *this;
    }

    LogStream &operator<<(std::ostream &(*manipulator)(std::ostream &))
    {
        string_stream << manipulator;

        return *this;
    }

    LogStream &operator<<(std::ios_base &(*manipulator)(std::ios_base &))
    {
        string_stream << manipulator;

        return *this;
    }

    std::string str() const { return string_stream.str(); }
};

using log_stream = LogStream;

} // namespace detail

/**
 * @brief Structured log entry captured in the in-memory ring buffer for UI
 * display.
 */
struct LogEntry
{
    /** @brief Formatted local timestamp string (e.g. "12:04:32.189"). */
    std::string timestamp;

    /** @brief Severity classification. */
    severity_level level = severity_level::info;

    /** @brief Category channel name (e.g. "server", "network", "orm", "routes",
     * "modules"). */
    std::string channel;

    /** @brief Source code filename where log originated. */
    std::string file;

    /** @brief Source code line number. */
    int line = 0;

    /** @brief Function name where log originated. */
    std::string function;

    /** @brief Evaluated message text. */
    std::string message;
};

/// Backward compatibility alias
using log_entry = LogEntry;

/**
 * @brief Thread-safe circular buffer maintaining recent log entries in memory.
 *
 * @details Retains the last N entries for live terminal rendering, dashboard
 * inspection, and error telemetry. Evicts oldest entries in FIFO order when
 * capacity is reached.
 *
 * Example:
 * @code{.cpp}
 * auto& ring_buffer = api::get_log_ring_buffer();
 * std::vector<api::LogEntry> recent_errors = ring_buffer.snapshot_errors();
 * for (const auto& entry : recent_errors)
 * {
 *     std::println("[{}] {}", entry.timestamp, entry.message);
 * }
 * @endcode
 */
class log_ring_buffer
{
  public:
    /**
     * @brief Constructs ring buffer with maximum capacity.
     *
     * @param[in] capacity Maximum number of log records to retain in memory.
     */
    explicit log_ring_buffer(size_t capacity = 1000);

    /**
     * @brief Appends a new log entry, evicting the oldest entry if at capacity.
     *
     * @param[in] entry Log record to store.
     */
    void push(LogEntry entry);

    /**
     * @brief Creates a thread-safe snapshot copy of all currently buffered log
     * entries.
     *
     * @return std::vector<LogEntry> Chronologically ordered entries from oldest
     * to newest.
     */
    std::vector<LogEntry> snapshot() const;

    /**
     * @brief Creates a thread-safe snapshot copy of warning, error, and fatal
     * entries only.
     *
     * @return std::vector<LogEntry> Filtered error entries.
     */
    std::vector<LogEntry> snapshot_errors() const;

    /**
     * @brief Returns the count of entries currently held in the buffer.
     *
     * @return size_t Current entry count.
     */
    size_t size() const;

  private:
    mutable std::mutex mutex_;
    std::deque<LogEntry> buffer_;
    size_t capacity_;
};

/**
 * @brief Initializes the Boost.Log subsystem according to ServerConfig
 * settings.
 *
 * @details Configures:
 * 1. Synchronous console text sink writing to std::clog.
 * 2. Rotating text file backend writing to log_dir (10 MB rotation limit).
 * 3. Minimum severity filtering based on configuration.log_level.
 *
 * @param[in] configuration Server configuration structure.
 */
void init_logging(const ServerConfig &configuration);

/**
 * @brief Accesses the global singleton in-memory log ring buffer.
 *
 * @return log_ring_buffer& Reference to global buffer.
 */
log_ring_buffer &get_log_ring_buffer();

/**
 * @brief Accesses the global multi-threaded Boost severity channel logger.
 *
 * @return boost::log::sources::severity_channel_logger_mt<severity_level,
 * std::string>& Logger reference.
 */
boost::log::sources::severity_channel_logger_mt<severity_level, std::string> &
get_logger();

/**
 * @brief Core log dispatching function invoked by logging macros.
 *
 * @param[in] channel Category channel name.
 * @param[in] level Log severity level.
 * @param[in] file Source filename.
 * @param[in] line Source line number.
 * @param[in] function_name Source function signature.
 * @param[in] message Formatted message body.
 */
void log_message(const std::string &channel, severity_level level,
                 const char *file, int line, const char *function_name,
                 const std::string &message);

} // namespace api

/**
 * @brief Emits a TRACE severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_TRACE("network", "Received byte packet of length " << length);
 * @endcode
 */
#define LOG_TRACE(channel, message)                                            \
    ::api::log_message(channel, ::api::severity_level::trace,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())

/**
 * @brief Emits a DEBUG severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_DEBUG("orm", "Constructed SQL query: " << query_string);
 * @endcode
 */
#define LOG_DEBUG(channel, message)                                            \
    ::api::log_message(channel, ::api::severity_level::debug,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())

/**
 * @brief Emits an INFO severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_INFO("server", "Server listening on port " << port);
 * @endcode
 */
#define LOG_INFO(channel, message)                                             \
    ::api::log_message(channel, ::api::severity_level::info,                   \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())

/**
 * @brief Emits a WARN severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_WARN("routes", "Route path conflict detected for endpoint: " << path);
 * @endcode
 */
#define LOG_WARN(channel, message)                                             \
    ::api::log_message(channel, ::api::severity_level::warning,                \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())

/**
 * @brief Emits an ERROR severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_ERROR("database", "Failed to connect to host: " << host_address);
 * @endcode
 */
#define LOG_ERROR(channel, message)                                            \
    ::api::log_message(channel, ::api::severity_level::error,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())

/**
 * @brief Emits a FATAL severity message to the specified logging channel.
 *
 * Example:
 * @code{.cpp}
 * LOG_FATAL("server", "Unable to bind TCP socket on port " << port);
 * @endcode
 */
#define LOG_FATAL(channel, message)                                            \
    ::api::log_message(channel, ::api::severity_level::fatal,                  \
                       ::api::detail::file_basename(__FILE__), __LINE__,       \
                       __func__,                                               \
                       (::api::detail::LogStream() << message).str())
