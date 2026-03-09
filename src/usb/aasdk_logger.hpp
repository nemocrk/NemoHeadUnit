#pragma once

#include <iostream>
#include <string>
#include <aasdk/common/ILogger.hpp>

namespace nemo {

class AasdkLogger : public aasdk::common::ILogger {
public:
    void log(Level level, const std::string& component, const std::string& message) override {
        std::string level_str;
        switch (level) {
            case Level::TRACE: level_str = "TRACE"; break;
            case Level::DEBUG: level_str = "DEBUG"; break;
            case Level::INFO:  level_str = "INFO "; break;
            case Level::WARN:  level_str = "WARN "; break;
            case Level::ERROR: level_str = "ERROR"; break;
            case Level::FATAL: level_str = "FATAL"; break;
            default:           level_str = "UNKNOWN"; break;
        }

        std::cout << "[" << level_str << "] [AASDK] [" << component << "] " << message << std::endl;
    }
};

} // namespace nemo
