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
    
    // Delegato per gestire quando la versione è stata controllata, si aspetta Handshake 1 o vuoto in fallback
    virtual void onVersionStatus(int major, int minor, int status) = 0;
    
    // Ritorna il pezzo di handshake da rimandare o "" se handshake completato / demandato
    virtual std::string onHandshake(const std::string& payload_bytes) = 0;
};

} // namespace nemo
