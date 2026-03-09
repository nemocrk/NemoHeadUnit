#pragma once

#include <string>
#include <memory>
#include <aasdk/Messenger/ICryptor.hpp>

namespace nemo {

class IOrchestrator {
public:
    virtual ~IOrchestrator() = default;

    virtual void setCryptor(std::shared_ptr<aasdk::messenger::ICryptor> cryptor) = 0;

    // Ritorna la risposta serializzata; se c'e' errore l'eccezione ferma il thread.
    virtual std::string onServiceDiscoveryRequest(const std::string& request_bytes) = 0;
    virtual std::string onPingRequest(const std::string& request_bytes) = 0;
    virtual std::string onAudioFocusRequest(const std::string& request_bytes) = 0;
    virtual std::string onVideoChannelOpenRequest(const std::string& request_bytes) = 0;

    // Innesca il primo chunk dell'handshake SSL
    virtual std::string onVersionStatus(int major, int minor, int status) = 0;

    // Riceve chunk e ritorna il prossimo, oppure "" se handshake completato
    virtual std::string onHandshake(const std::string& payload_bytes) = 0;

    // Dopo onHandshake che ritorna "": C++ chiede i byte AuthComplete
    virtual std::string getAuthCompleteResponse() = 0;
};

} // namespace nemo
