#pragma once

#include <aasdk/Common/ModernLogger.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>

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

        // ---------------------------------------------------------------------------
        // Imposta il livello minimo per un singolo component (es. "app.av_core.audio").
        // Chiamato una sola volta dalla configurazione Python: zero overhead runtime.
        // Thread-safe via shared_mutex (write esclusivo, read condiviso).
        // ---------------------------------------------------------------------------
        void setComponentLevel(const std::string &component, AppLogLevel level)
        {
            std::unique_lock lock(component_levels_mutex_);
            if (component.empty())
            {
                // Stringa vuota → livello globale
                min_level_ = level;
                return;
            }
            component_levels_[component] = level;
        }

        void log(AppLogLevel level,
                 const char *component,
                 const char *function,
                 const char *file,
                 int line,
                 const std::string &message)
        {
            // Bug #1 fix: shouldLog() viene controllato SEMPRE, prima di qualsiasi
            // dispatch (Python o stdout). Se il livello non passa, usciamo subito
            // senza acquisire il GIL né costruire stringhe.
            if (!shouldLogFor(level, component))
                return;

            if (g_python_log_fn)
            {
                // g_python_should_log_fn è già stato valutato in shouldLogFor();
                // qui chiamiamo solo il dispatcher effettivo.
                const auto aasdk_level = toAasdkLevel(level);
                g_python_log_fn(component, static_cast<int>(aasdk_level), message);
                return;
            }

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

        // Controllo livello globale puro (usato dal fallback stdout).
        bool shouldLog(AppLogLevel level) const
        {
            return static_cast<int>(level) >= static_cast<int>(min_level_);
        }

        // ---------------------------------------------------------------------------
        // shouldLogFor: hot path — ZERO GIL, ZERO chiamate Python.
        //
        // Algoritmo:
        //   1. Cerca il component nella cache C++ (shared_lock → reads paralleli OK).
        //   2. Longest-prefix match: "app.av_core.audio" → "app.av_core" → "app" → global.
        //   3. Solo se il livello supera la soglia C++, il trampoline Python viene
        //      invocato (caso raro: g_python_should_log_fn impostato E frame che
        //      passa la cache). In pratica non accade nel steady state perché
        //      set_module_level() sincronizza già la cache C++.
        // ---------------------------------------------------------------------------
        bool shouldLogFor(AppLogLevel level, const char *component) const
        {
            const AppLogLevel effective = effectiveLevelFor(component);
            if (static_cast<int>(level) < static_cast<int>(effective))
                return false;

            // Filtro Python opzionale (solo se non ancora coperto dalla cache).
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

        // Longest-prefix match nella cache component_levels_.
        // Chiamato sotto shared_lock.
        AppLogLevel effectiveLevelFor(const char *component) const
        {
            if (!component || component[0] == '\0')
                return min_level_;

            std::shared_lock lock(component_levels_mutex_);

            if (component_levels_.empty())
                return min_level_;

            // Cerca match esatto prima (caso comune, O(1)).
            {
                auto it = component_levels_.find(component);
                if (it != component_levels_.end())
                    return it->second;
            }

            // Longest-prefix match: "app.av_core.audio" → prova "app.av_core" → "app".
            std::string key(component);
            while (!key.empty())
            {
                const auto dot = key.rfind('.');
                if (dot == std::string::npos)
                    break;
                key.resize(dot);
                auto it = component_levels_.find(key);
                if (it != component_levels_.end())
                    return it->second;
            }

            return min_level_;
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

        // Cache per-component: scritta raramente (configurazione),
        // letta frequentemente (ogni log statement). shared_mutex ideale.
        mutable std::shared_mutex component_levels_mutex_;
        std::unordered_map<std::string, AppLogLevel> component_levels_;
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
