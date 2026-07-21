#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <ctime>
#include <thread>
#include <algorithm>
#include <atomic>

namespace fs = std::filesystem;

// Helper struct to represent a date
struct Date {
    int year;
    int month;
    int day;

    bool operator!=(const Date& other) const {
        return year != other.year || month != other.month || day != other.day;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << year << '-'
            << std::setw(2) << month << '-'
            << std::setw(2) << day;
        return oss.str();
    }

    static Date currentDate() {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &tt);
#else
        localtime_r(&tt, &local_tm);
#endif
        Date d;
        d.year = local_tm.tm_year + 1900;
        d.month = local_tm.tm_mon + 1;
        d.day = local_tm.tm_mday;
        return d;
    }
};

// --- LogWorker implementation ---
class LogWorker {
public:
    LogWorker(const std::string &dirPath, const std::string &baseName, const std::string &ext,
              int64_t maxBytes, int maxFiles, bool forceMidnight)
        : m_dirPath(dirPath), m_baseName(baseName), m_ext(ext),
        m_maxBytes(maxBytes), m_maxFiles(maxFiles),
        m_running(true), m_forceMidnight(forceMidnight), m_writeCount(0)
    {
        m_currentDate = Date::currentDate();
        m_currentFilePath = computePathForDate(m_currentDate);
        m_thread = std::thread(&LogWorker::run, this);
    }

    ~LogWorker() {
        stop();
    }

    void push(const std::string &line) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(line);
        m_cv.notify_one();
    }

    void flushSync() {
        std::lock_guard<std::mutex> lock(m_mutex);
        writeAllUnlocked();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) return;
            m_running = false;
            m_cv.notify_one();
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    void run() {
        openFileForDate(m_currentDate);

        while (true) {
            std::unique_lock<std::mutex> lock(m_mutex);

            if (!m_running && m_queue.empty()) {
                // final flush
                writeAllUnlocked();
                if (m_outFile.is_open()) {
                    m_outFile.flush();
                    m_outFile.close();
                }
                return;
            }

            if (m_queue.empty()) {
                // If forced midnight rotation is enabled, compute exact ms until next midnight and wait that long.
                if (m_forceMidnight) {
                    int64_t msUntilMidnight = computeMsUntilNextMidnight();
                    if (msUntilMidnight <= 0) msUntilMidnight = 1000;
                    m_cv.wait_for(lock, std::chrono::milliseconds(msUntilMidnight));
                    Date now = Date::currentDate();
                    if (now != m_currentDate) {
                        m_currentDate = now;
                        m_currentFilePath = computePathForDate(m_currentDate);
                        openFileForDate(m_currentDate);
                    }
                    writeAllUnlocked();
                } else {
                    m_cv.wait_for(lock, std::chrono::milliseconds(2000));
                    Date now = Date::currentDate();
                    if (now != m_currentDate) {
                        m_currentDate = now;
                        m_currentFilePath = computePathForDate(m_currentDate);
                        openFileForDate(m_currentDate);
                    }
                    writeAllUnlocked();
                }
            } else {
                writeAllUnlocked();
            }
        }
    }

    std::string computePathForDate(const Date &date) const {
        std::string fileName = m_baseName + "-" + date.toString() + m_ext;
        fs::path dirPath(m_dirPath);
        return (dirPath / fileName).string();
    }

    int64_t computeMsUntilNextMidnight() const {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
#ifdef _WIN32
        localtime_s(&local_tm, &tt);
#else
        localtime_r(&tt, &local_tm);
#endif
        
        // Create next midnight
        std::tm next_midnight = local_tm;
        next_midnight.tm_hour = 0;
        next_midnight.tm_min = 0;
        next_midnight.tm_sec = 0;
        next_midnight.tm_mday += 1;
        
        std::time_t next_mid_tt = std::mktime(&next_midnight);
        auto next_mid = std::chrono::system_clock::from_time_t(next_mid_tt);
        
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(next_mid - now).count();
        if (ms < 0) ms = 0;
        return ms;
    }

    void openFileForDate(const Date &date) {
        if (m_outFile.is_open()) {
            m_outFile.flush();
            m_outFile.close();
        }

        fs::path filePath(m_currentFilePath);
        fs::path dirPath = filePath.parent_path();
        
        if (!dirPath.empty()) {
            std::error_code ec;
            fs::create_directories(dirPath, ec);
            if (ec) {
                fprintf(stderr, "Logger: cannot create directory %s: %s\n", 
                        dirPath.string().c_str(), ec.message().c_str());
            }
        }

        m_outFile.open(m_currentFilePath, std::ios::app);
        if (!m_outFile.is_open()) {
            fprintf(stderr, "Logger: cannot open %s\n", m_currentFilePath.c_str());
        }
    }

    void rotateIfNeededUnlocked() {
        if (m_maxBytes <= 0) return;
        if (!m_outFile.is_open()) return;

        // Get current file size
        std::error_code ec;
        auto fileSize = fs::file_size(m_currentFilePath, ec);
        if (ec || fileSize < static_cast<uintmax_t>(m_maxBytes)) return;

        m_outFile.flush();
        m_outFile.close();

        // Rotate files
        for (int i = m_maxFiles - 1; i >= 0; --i) {
            std::string src = (i == 0) ? m_currentFilePath : m_currentFilePath + "." + std::to_string(i);
            std::string dst = m_currentFilePath + "." + std::to_string(i + 1);
            
            std::error_code ec;
            if (fs::exists(dst, ec)) {
                fs::remove(dst, ec);
            }
            if (fs::exists(src, ec)) {
                fs::rename(src, dst, ec);
            }
        }

        m_outFile.open(m_currentFilePath, std::ios::app);
        if (!m_outFile.is_open()) {
            fprintf(stderr, "Logger: cannot reopen %s after rotation\n", m_currentFilePath.c_str());
        }
    }

    void writeAllUnlocked() {
        while (!m_queue.empty()) {
            std::string line = m_queue.front();
            m_queue.pop_front();

            Date now = Date::currentDate();
            if (now != m_currentDate) {
                m_currentDate = now;
                m_currentFilePath = computePathForDate(m_currentDate);
                openFileForDate(m_currentDate);
            }

            // Always print to console for visibility
            fprintf(stdout, "%s\n", line.c_str());
            fflush(stdout);

            // Also write to file if open
            if (m_outFile.is_open()) {
                m_outFile << line << std::endl;
            }

            if (++m_writeCount >= 32) {
                m_writeCount = 0;
                rotateIfNeededUnlocked();
            }
        }
        rotateIfNeededUnlocked();
        if (m_outFile.is_open()) {
            m_outFile.flush();
        }
    }

    // members
    std::string m_dirPath;
    std::string m_baseName;
    std::string m_ext;
    int64_t m_maxBytes;
    int m_maxFiles;
    bool m_forceMidnight;

    Date m_currentDate;
    std::string m_currentFilePath;

    std::ofstream m_outFile;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;
    std::atomic<bool> m_running;
    int m_writeCount;
    
    std::thread m_thread;
};

// --- Logger implementation ---

// static members initialization
Logger* Logger::s_instance = nullptr;
std::mutex Logger::s_instanceMutex;

// Order-response secondary logger
static LogWorker* s_orderResponseWorker = nullptr;
static std::mutex s_orderResponseMutex;

// pre-init buffer statics
std::mutex Logger::s_preInitMutex;
std::deque<std::string> Logger::s_preInitBuffer;
int Logger::s_preInitMaxRecords = 50; // 50 lines before logger init

Logger::Logger()
    : m_worker(nullptr), m_maxBytes(0), m_maxFiles(0), m_level(DebugLevel), m_forceMidnightRotate(false) {}

Logger::~Logger() {
    if (m_worker) {
        m_worker->stop();
        delete m_worker;
        m_worker = nullptr;
    }
}

Logger* Logger::instance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance = new Logger();
    }
    return s_instance;
}

static void splitPath(const std::string &input, std::string &dirOut, std::string &baseOut, std::string &extOut) {
    fs::path p(input);
    dirOut = p.parent_path().string();
    std::string filename = p.filename().string();
    
    // Find extension
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > 0) {
        baseOut = filename.substr(0, dotPos);
        extOut = filename.substr(dotPos);
    } else {
        baseOut = filename;
        extOut = ".log";
    }
}

void Logger::init(const std::string &filePath, LogLevel level, int64_t maxBytes, int maxFiles) {
    Logger* L = instance();
    std::lock_guard<std::mutex> lock(s_instanceMutex);

    // set pre-init max
    {
        std::lock_guard<std::mutex> preLocker(s_preInitMutex);
        // if buffer currently larger than new max, drop oldest
        while ((int)s_preInitBuffer.size() > s_preInitMaxRecords) {
            s_preInitBuffer.pop_front();
        }
    }

    // if re-init, stop previous worker
    if (L->m_worker) {
        L->m_worker->stop();
        delete L->m_worker;
        L->m_worker = nullptr;
    }

    L->m_basePath = filePath;
    splitPath(filePath, L->m_dirPath, L->m_baseName, L->m_ext);

    L->m_maxBytes = maxBytes;
    L->m_maxFiles = std::max(1, maxFiles);
    L->m_level = level;
    L->m_forceMidnightRotate = true;

    // create worker with dir + base + ext + midnight flag
    L->m_worker = new LogWorker(L->m_dirPath, L->m_baseName, L->m_ext, L->m_maxBytes, L->m_maxFiles, L->m_forceMidnightRotate);

    // Drain pre-init buffer into worker (atomic swap)
    {
        std::deque<std::string> drained;
        {
            std::lock_guard<std::mutex> preLocker(s_preInitMutex);
            drained.swap(s_preInitBuffer); // drained now holds buffered lines
        }
        // enqueue into worker
        for (const std::string &line : drained) {
            if (L->m_worker) {
                L->m_worker->push(line);
            } else {
                // fallback (shouldn't happen) to stdout
                fprintf(stdout, "%s\n", line.c_str());
                fflush(stdout);
            }
        }
    }
}

void Logger::initOrderResponse(const std::string &filePath, int64_t maxBytes, int maxFiles) {
    std::lock_guard<std::mutex> lock(s_orderResponseMutex);
    if (s_orderResponseWorker) {
        s_orderResponseWorker->stop();
        delete s_orderResponseWorker;
        s_orderResponseWorker = nullptr;
    }
    std::string dir, base, ext;
    splitPath(filePath, dir, base, ext);
    s_orderResponseWorker = new LogWorker(dir, base, ext, maxBytes, std::max(1, maxFiles), true);
}

void Logger::logOrderResponse(const std::string &msg) {
    // Format timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    std::ostringstream ts;
    ts << std::setfill('0')
       << std::setw(4) << (local_tm.tm_year + 1900) << '-'
       << std::setw(2) << (local_tm.tm_mon + 1)     << '-'
       << std::setw(2) << local_tm.tm_mday           << ' '
       << std::setw(2) << local_tm.tm_hour           << ':'
       << std::setw(2) << local_tm.tm_min            << ':'
       << std::setw(2) << local_tm.tm_sec            << '.'
       << std::setw(3) << ms.count();

    std::string line = ts.str() + " [ORDER_RESPONSE] " + msg;

    std::lock_guard<std::mutex> lock(s_orderResponseMutex);
    if (s_orderResponseWorker) {
        s_orderResponseWorker->push(line);
    } else {
        fprintf(stdout, "%s\n", line.c_str());
        fflush(stdout);
    }
}

void Logger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(s_orderResponseMutex);
        if (s_orderResponseWorker) {
            s_orderResponseWorker->stop();
            delete s_orderResponseWorker;
            s_orderResponseWorker = nullptr;
        }
    }
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) return;
    if (s_instance->m_worker) {
        s_instance->m_worker->stop();
        delete s_instance->m_worker;
        s_instance->m_worker = nullptr;
    }
    delete s_instance;
    s_instance = nullptr;
}

void Logger::setLevel(Logger::LogLevel level) {
    Logger* L = instance();
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    L->m_level = level;
}

Logger::LogLevel Logger::level() {
    Logger* L = instance();
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    return L->m_level;
}

static inline std::string levelToString(Logger::LogLevel l) {
    switch (l) {
    case Logger::DebugLevel: return "DEBUG";
    case Logger::InfoLevel: return "INFO";
    case Logger::WarningLevel: return "WARN";
    case Logger::ErrorLevel: return "ERROR";
    case Logger::CriticalLevel: return "CRIT";
    case Logger::FatalLevel: return "FATAL";
    }
    return "UNKNOWN";
}

std::string Logger::maskSensitive(char maskChar)
{
    return std::string(4, maskChar);
}

void Logger::log(Logger::LogLevel level, const std::string &msg, const char *file, int line, const char *func) {
    if (!s_instance) instance(); // ensure singleton exists

    // quick level filter
    {
        std::lock_guard<std::mutex> lock(s_instanceMutex);
        if (level < s_instance->m_level && level != FatalLevel) return;
    }

    // Get current time
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif

    std::ostringstream timeStream;
    timeStream << std::setfill('0')
               << std::setw(4) << (local_tm.tm_year + 1900) << '-'
               << std::setw(2) << (local_tm.tm_mon + 1) << '-'
               << std::setw(2) << local_tm.tm_mday << ' '
               << std::setw(2) << local_tm.tm_hour << ':'
               << std::setw(2) << local_tm.tm_min << ':'
               << std::setw(2) << local_tm.tm_sec << '.'
               << std::setw(3) << ms.count();

    std::ostringstream threadIdStream;
    threadIdStream << std::hex << std::this_thread::get_id();
    
    std::string location;
    if (file) {
        std::string filePath(file);
        size_t pos1 = filePath.find_last_of('/');
        size_t pos2 = filePath.find_last_of('\\');
        size_t pos = (pos1 == std::string::npos) ? pos2 : 
                     (pos2 == std::string::npos) ? pos1 : 
                     std::max(pos1, pos2);

        std::string filename = (pos != std::string::npos) ? filePath.substr(pos + 1) : filePath;
        location = " (" + filename + ":" + std::to_string(line) + ")";
    } else if (func) {
        location = " (" + std::string(func) + ":" + std::to_string(line) + ")";
    }

    std::string logLine = timeStream.str() + " [" + levelToString(level) + "] [0x" + 
                         threadIdStream.str() + "] " + msg + location;

    // If worker available, push
    if (s_instance && s_instance->m_worker) {
        if (level == FatalLevel) {
            s_instance->m_worker->push(logLine);
            s_instance->m_worker->flushSync();
            abort();
            return;
        } else {
            s_instance->m_worker->push(logLine);
            return;
        }
    }

    // Pre-init: print to stdout and buffer
    {
        fprintf(stdout, "%s\n", logLine.c_str());
        fflush(stdout);

        std::lock_guard<std::mutex> preLocker(s_preInitMutex);
        if ((int)s_preInitBuffer.size() >= s_preInitMaxRecords) {
            s_preInitBuffer.pop_front();
        }
        s_preInitBuffer.push_back(logLine);
    }

    if (level == FatalLevel) {
        abort();
    }
}
