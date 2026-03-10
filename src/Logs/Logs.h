#ifndef LOGS_H
#define LOGS_H

#include <iostream>
#include <syncstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <thread>

#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"

#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"


enum class Level
{
    TRACE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

class Logs
{
private:
    static std::mutex _mutex;

    static std::string names(Level level)
    {
        switch(level)
        {
        case Level::TRACE:
            return "TRACE";
        case Level::DEBUG:
            return "DEBUG";
        case Level::INFO:
            return "INFO";
        case Level::WARNING:
            return "WARNING";
        case Level::ERROR:
            return "ERROR";
        case Level::FATAL:
            return "FATAL";
        default:
            return "[UNKNOWN]";
        }
    }

    static std::string colors(Level level)
    {
        switch(level)
        {
        case Level::TRACE:
            return DIM;
        case Level::DEBUG:
            return CYAN;
        case Level::INFO:
            return CYAN;
        case Level::WARNING:
            return YELLOW;
        case Level::ERROR:
            return RED;
        case Level::FATAL:
            return YELLOW;
        default:
            return RESET;
        }
    }

public:
    static void write(Level level, const std::string& message, const char* file, int line)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        std::string path(file);
        size_t position = path.find("src/");
        std::string trim = (position != std::string::npos) ? path.substr(position) : path;
        
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);

        std::osyncstream(std::cout) << WHITE << std::put_time(std::localtime(&time), "%H:%M:%S") << RESET
            << " [" << colors(level) << names(level) << RESET << "]"
            << " [" << YELLOW << trim << RESET << ":" << YELLOW << line << RESET << "]"
            << " [" << GREEN << std::this_thread::get_id() << RESET << "] "
            << message << std::endl << "[CMD] >> " << std::flush;
    }
};

#define LOG(level, msg) \
    Logs::write(level, msg, __FILE__, __LINE__)

#define LOG_TRACE(msg) \
    Logs::write(Level::TRACE, msg, __FILE__, __LINE__)

#define LOG_DEBUG(msg) \
    Logs::write(Level::DEBUG, msg, __FILE__, __LINE__)

#define LOG_INFO(msg) \
    Logs::write(Level::INFO, msg, __FILE__, __LINE__)

#define LOG_WARNING(msg) \
    Logs::write(Level::WARNING, msg, __FILE__, __LINE__)

#define LOG_ERROR(msg) \
    Logs::write(Level::ERROR, msg, __FILE__, __LINE__)

#define LOG_FATAL(msg) \
    Logs::write(Level::FATAL, msg, __FILE__, __LINE__)

#endif // LOGS_H