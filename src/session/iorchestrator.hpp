#pragma once

#include <string>

namespace nemo {

class IOrchestrator {
public:
    virtual ~IOrchestrator() = default;
    virtual std::string onServiceDiscoveryRequest(const std::string& request_bytes) = 0;
    virtual std::string onPingRequest(const std::string& request_bytes) = 0;
    virtual std::string onAudioFocusRequest(const std::string& request_bytes) = 0;
    virtual std::string onVideoChannelOpenRequest(const std::string& request_bytes) = 0;
};

} // namespace nemo
