#pragma once

#include <aasdk/Common/ModernLogger.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace nemo
{

    using PythonLogFn = void (*)(const std::string &component, int level, const std::string &message);
    using PythonShouldLogFn = bool (*)(const std::string &component, int level);

    inline PythonLogFn g_python_log_fn = nullptr;
    inline PythonShouldLogFn g_python_should_log_fn = nullptr;

    enum class AppLogLevel
    {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4
    };

    class AppLogger
    {
    public:
        static AppLogger &instance()
        {
            static AppLogger logger;
            return logger;
        }

        void log(AppLogLevel level,
                 const char *component,
                 const char *function,
                 const char *file,
                 int line,
                 const std::string &message)
        {
            // Allow Python-driven logging to short-circuit the C++ output.
            if (g_python_log_fn)
            {
                const auto aasdk_level = toAasdkLevel(level);
                if (g_python_should_log_fn && !g_python_should_log_fn(component, static_cast<int>(aasdk_level)))
                {
                    return;
                }
                g_python_log_fn(component, static_cast<int>(aasdk_level), message);
                return;
            }

            if (!shouldLog(level))
                return;

            const std::string line_out = formatLine(level, component, message);
            if (level >= AppLogLevel::ERROR)
            {
                std::cerr << line_out << std::endl;
            }
            else
            {
                std::cout << line_out << std::endl;
            }

            if (use_modern_logger_)
            {
                auto &logger = aasdk::common::ModernLogger::getInstance();
                const auto aasdk_level = toAasdkLevel(level);
                if (logger.shouldLog(aasdk_level, aasdk::common::LogCategory::AUDIO))
                {
                    logger.log(aasdk_level,
                               aasdk::common::LogCategory::AUDIO,
                               component,
                               function ? function : "",
                               file ? file : "",
                               line,
                               message);
                }
            }
        }

        bool shouldLog(AppLogLevel level) const
        {
            return static_cast<int>(level) >= static_cast<int>(min_level_);
        }

        bool shouldLogFor(AppLogLevel level, const char *component) const
        {
            if (!shouldLog(level))
                return false;

            if (g_python_should_log_fn)
            {
                return g_python_should_log_fn(component, static_cast<int>(toAasdkLevel(level)));
            }

            return true;
        }

    private:
        AppLogger()
        {
            min_level_ = parseLevel(std::getenv("CORE_LOG"));
            use_modern_logger_ = envTruthy(std::getenv("NEMO_AVCORE_LOG_TO_AASDK"));
        }

        static bool envTruthy(const char *value)
        {
            if (!value || !*value)
                return false;
            std::string s(value);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return (s == "1" || s == "true" || s == "yes" || s == "on");
        }

        static AppLogLevel parseLevel(const char *value)
        {
            if (!value || !*value)
                return AppLogLevel::INFO;
            std::string s(value);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (s == "trace" || s == "verbose" || s == "2")
                return AppLogLevel::TRACE;
            if (s == "debug" || s == "1" || s == "true" || s == "yes" || s == "on")
                return AppLogLevel::DEBUG;
            if (s == "info")
                return AppLogLevel::INFO;
            if (s == "warn" || s == "warning")
                return AppLogLevel::WARN;
            if (s == "error")
                return AppLogLevel::ERROR;
            return AppLogLevel::INFO;
        }

        static aasdk::common::LogLevel toAasdkLevel(AppLogLevel level)
        {
            switch (level)
            {
            case AppLogLevel::TRACE:
                return aasdk::common::LogLevel::TRACE;
            case AppLogLevel::DEBUG:
                return aasdk::common::LogLevel::DEBUG;
            case AppLogLevel::INFO:
                return aasdk::common::LogLevel::INFO;
            case AppLogLevel::WARN:
                return aasdk::common::LogLevel::WARN;
            case AppLogLevel::ERROR:
                return aasdk::common::LogLevel::ERROR;
            }
            return aasdk::common::LogLevel::INFO;
        }

        static const char *levelName(AppLogLevel level)
        {
            switch (level)
            {
            case AppLogLevel::TRACE:
                return "TRACE";
            case AppLogLevel::DEBUG:
                return "DEBUG";
            case AppLogLevel::INFO:
                return "INFO";
            case AppLogLevel::WARN:
                return "WARN";
            case AppLogLevel::ERROR:
                return "ERROR";
            }
            return "INFO";
        }

        static std::string formatLine(AppLogLevel level, const char *component, const std::string &message)
        {
            using clock = std::chrono::system_clock;
            const auto now = clock::now();
            const auto now_time = clock::to_time_t(now);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch()) %
                            1000;

            std::tm tm{};
            localtime_r(&now_time, &tm);

            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
                << "," << std::setw(3) << std::setfill('0') << ms.count()
                << " [" << levelName(level) << "] "
                << (component ? component : "app.av_core")
                << ": " << message;
            return oss.str();
        }

        AppLogLevel min_level_{AppLogLevel::INFO};
        bool use_modern_logger_{false};
    };

    class LogStream
    {
    public:
        LogStream(AppLogLevel level, const char *component, const char *function, const char *file, int line)
            : level_(level), component_(component), function_(function), file_(file), line_(line)
        {
        }

        ~LogStream()
        {
            AppLogger::instance().log(level_, component_, function_, file_, line_, stream_.str());
        }

        template <typename T>
        LogStream &operator<<(const T &value)
        {
            stream_ << value;
            return *this;
        }

    private:
        AppLogLevel level_;
        const char *component_;
        const char *function_;
        const char *file_;
        int line_;
        std::ostringstream stream_;
    };

} // namespace nemo

#define APP_LOG_TRACE(component)                                                                                               \
    for (bool _once = ::nemo::AppLogger::instance().shouldLogFor(::nemo::AppLogLevel::TRACE, component); _once; _once = false) \
    ::nemo::LogStream(::nemo::AppLogLevel::TRACE, component, __FUNCTION__, __FILE__, __LINE__)

#define APP_LOG_DEBUG(component)                                                                                               \
    for (bool _once = ::nemo::AppLogger::instance().shouldLogFor(::nemo::AppLogLevel::DEBUG, component); _once; _once = false) \
    ::nemo::LogStream(::nemo::AppLogLevel::DEBUG, component, __FUNCTION__, __FILE__, __LINE__)

#define APP_LOG_INFO(component)                                                                                               \
    for (bool _once = ::nemo::AppLogger::instance().shouldLogFor(::nemo::AppLogLevel::INFO, component); _once; _once = false) \
    ::nemo::LogStream(::nemo::AppLogLevel::INFO, component, __FUNCTION__, __FILE__, __LINE__)

#define APP_LOG_WARN(component)                                                                                               \
    for (bool _once = ::nemo::AppLogger::instance().shouldLogFor(::nemo::AppLogLevel::WARN, component); _once; _once = false) \
    ::nemo::LogStream(::nemo::AppLogLevel::WARN, component, __FUNCTION__, __FILE__, __LINE__)

#define APP_LOG_ERROR(component)                                                                                               \
    for (bool _once = ::nemo::AppLogger::instance().shouldLogFor(::nemo::AppLogLevel::ERROR, component); _once; _once = false) \
    ::nemo::LogStream(::nemo::AppLogLevel::ERROR, component, __FUNCTION__, __FILE__, __LINE__)
