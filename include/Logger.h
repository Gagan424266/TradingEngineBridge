#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <sstream>
#include <type_traits>


// Helper to convert many common inputs into std::string.
inline std::string LOG_MAKE_MSG(const std::string &s) { return s; }
inline std::string LOG_MAKE_MSG(const char *s) { return s ? std::string(s) : std::string(); }

// Fallback template for numeric and other types
template<typename T>
inline std::string LOG_MAKE_MSG(const T &v) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(v);
    } else {
        // For types with << operator
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }
}

// Core macros — pass __func__, __LINE__ automatically.
#define LOG_DEBUG(msg)  Logger::log(Logger::DebugLevel,    LOG_MAKE_MSG(msg), __FILE__, __LINE__, __func__)
#define LOG_INFO(msg)   Logger::log(Logger::InfoLevel,     LOG_MAKE_MSG(msg))
#define LOG_WARN(msg)   Logger::log(Logger::WarningLevel,  LOG_MAKE_MSG(msg), __FILE__, __LINE__, __func__)
#define LOG_ERROR(msg)  Logger::log(Logger::ErrorLevel,    LOG_MAKE_MSG(msg), __FILE__, __LINE__, __func__)
#define LOG_CRIT(msg)   Logger::log(Logger::CriticalLevel, LOG_MAKE_MSG(msg), __FILE__, __LINE__, __func__)
#define LOG_FATAL(msg)  Logger::log(Logger::FatalLevel,    LOG_MAKE_MSG(msg), __FILE__, __LINE__, __func__)

// Function enter/exit helpers (convenient for tracing)
#define LOG_FUNC_ENTER() LOG_DEBUG(std::string("ENTER ") + std::string(__func__))
#define LOG_FUNC_EXIT()  LOG_DEBUG(std::string("EXIT  ") + std::string(__func__))



class LogWorker;

class Logger
{
public:
    enum LogLevel {
        DebugLevel = 0,
        InfoLevel,
        WarningLevel,
        ErrorLevel,
        CriticalLevel,
        FatalLevel
    };

    // filePath: "logs/app.log" or "logs/app"
    // forceMidnightRotate: if true, worker will wait until local midnight and force date rollover immediately
    // preInitBufferMax: maximum number of pre-init records to keep (default 50)
    static void init(const std::string &filePath,
                     LogLevel level = DebugLevel,
                     int64_t maxBytes = 10 * 1024 * 1024,
                     int maxFiles = 5);

    // Initialise a separate log file for OMS order responses.
    // Call once after Logger::init(). File rotates daily, same as the main log.
    static void initOrderResponse(const std::string &filePath,
                                  int64_t maxBytes = 10 * 1024 * 1024,
                                  int maxFiles = 5);

    // Write one line to the order-response log (thread-safe).
    static void logOrderResponse(const std::string &msg);

    static void shutdown();

    static void setLevel(LogLevel level);
    static LogLevel level();

    static std::string maskSensitive(char maskChar = '*');

    // Programmatic logging (thread safe)
    static void log(LogLevel level, const std::string &msg, const char *file = nullptr, int line = 0, const char *func = nullptr);

    // get singleton instance
    static Logger* instance();

private:
    Logger();
    ~Logger();

    // Delete copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // singleton instance + mutex
    static Logger* s_instance;
    static std::mutex s_instanceMutex;

    // pre-init buffer and mutex (shared across translation units)
    static std::mutex s_preInitMutex;
    static std::deque<std::string> s_preInitBuffer;
    static int s_preInitMaxRecords;

    // instance fields
    LogWorker* m_worker;
    std::string m_basePath;   // full provided path, e.g. "logs/app.log" or "logs/app"
    std::string m_baseName;   // "app"
    std::string m_dirPath;    // "logs"
    std::string m_ext;        // ".log"
    int64_t m_maxBytes;
    int m_maxFiles;
    LogLevel m_level;
    bool m_forceMidnightRotate;

    friend class LogWorker;
};