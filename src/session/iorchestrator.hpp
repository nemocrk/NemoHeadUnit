#pragma once

#include <string>
#include <memory>
#include <aasdk/Messenger/ICryptor.hpp>

namespace nemo {

class IOrchestrator {
public:
    virtual ~IOrchestrator() = default;
    
    // Chiamato dal C++ non appena il Cryptor e' pronto per passarlo al Python
    virtual void setCryptor(std::shared_ptr<aasdk::messenger::ICryptor> cryptor) = 0;

    virtual std::string onServiceDiscoveryRequest(const std::string& request_bytes) = 0;
    virtual std::string onPingRequest(const std::string& request_bytes) = 0;
    virtual std::string onAudioFocusRequest(const std::string& request_bytes) = 0;
    virtual std::string onVideoChannelOpenRequest(const std::string& request_bytes) = 0;
    
    virtual void onVersionStatus(int major, int minor, int status) = 0;
    
    // Return next handshake chunk, oppure bytes vuoti per indicare terminazione
    virtual std::string onHandshake(const std::string& payload_bytes) = 0;
};

} // namespace nemo
