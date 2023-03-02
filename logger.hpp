#ifndef _LOGGER_HPP
#define _LOGGER_HPP

#include <boost/thread.hpp>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fstream>
#include <ostream>
#include <regex>
#include <string>
#include <type_traits>
#include <unordered_map>

#ifndef TIMEBUF_SIZE
    #define TIMEBUF_SIZE 128
#else
    #define TIMEBUF_SIZE_DEFINED
#endif

enum class LogLevel : unsigned int {
    Error = 0b1,
    Warning = 0b10,
    Info = 0b100,
    Debug = 0b1000,
};

inline LogLevel operator|(LogLevel lhs, LogLevel rhs) {
    return static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) |
        static_cast<std::underlying_type<LogLevel>::type>(rhs));
}

inline LogLevel operator&(LogLevel lhs, LogLevel rhs) {
    return static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) &
        static_cast<std::underlying_type<LogLevel>::type>(rhs));
}

inline LogLevel operator^(LogLevel lhs, LogLevel rhs) {
    return static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) ^
        static_cast<std::underlying_type<LogLevel>::type>(rhs));
}

inline LogLevel operator~(LogLevel rhs) {
    return static_cast<LogLevel>(
        ~static_cast<std::underlying_type<LogLevel>::type>(rhs));
}

inline LogLevel& operator|=(LogLevel& lhs, LogLevel rhs) {
    lhs = static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) |
        static_cast<std::underlying_type<LogLevel>::type>(rhs));

    return lhs;
}

inline LogLevel& operator&=(LogLevel& lhs, LogLevel rhs) {
    lhs = static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) &
        static_cast<std::underlying_type<LogLevel>::type>(rhs));

    return lhs;
}

inline LogLevel& operator^=(LogLevel& lhs, LogLevel rhs) {
    lhs = static_cast<LogLevel>(
        static_cast<std::underlying_type<LogLevel>::type>(lhs) ^
        static_cast<std::underlying_type<LogLevel>::type>(rhs));

    return lhs;
}

class Logger {
private:
    boost::mutex mtx;

    static std::string filter(const std::string& str) {
        const static std::regex ansi_escape_code_re(R"(\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~]))", std::regex_constants::optimize);
        return std::regex_replace(str, ansi_escape_code_re, std::string());
    }

    void log(std::string message, bool filter_message = true) {
        if (filter_message) message = filter(message);

        time_t rawtime;
        struct tm* timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        char timef[TIMEBUF_SIZE];
        strftime(timef, TIMEBUF_SIZE, "%c", timeinfo);

        boost::unique_lock<boost::mutex> lock(this->mtx);

        std::ofstream logfile(this->logfile_name, std::ios::app);
        if (logfile.is_open()) {
            logfile << "[" << timef << "] " << message << std::endl;
            logfile.close();
        } else {
            throw std::runtime_error(strerror(errno));
        }
    }

public:
    std::string logfile_name;
    LogLevel log_level;

    Logger(const std::string& logfile_name, LogLevel log_level = LogLevel::Error | LogLevel::Warning | LogLevel::Info | LogLevel::Debug):
        logfile_name(logfile_name),
        log_level(log_level) {}

    inline void error(const std::string& message, bool filter_message = true) {
        if (static_cast<bool>(log_level & LogLevel::Error)) {
            log("Error: " + message, filter_message);
        }
    }

    inline void warn(const std::string& message, bool filter_message = true) {
        if (static_cast<bool>(log_level & LogLevel::Warning)) {
            log("Warning: " + message, filter_message);
        }
    }

    inline void info(const std::string& message, bool filter_message = true) {
        if (static_cast<bool>(log_level & LogLevel::Info)) {
            log("Info: " + message, filter_message);
        }
    }

    inline void debug(const std::string& message, bool filter_message = true) {
        if (static_cast<bool>(log_level & LogLevel::Debug)) {
            log("Debug: " + message, filter_message);
        }
    }
};

#ifndef TIMEBUF_SIZE_DEFINED
    #undef TIMEBUF_SIZE
#else
    #undef TIMEBUF_SIZE_DEFINED
#endif
#endif