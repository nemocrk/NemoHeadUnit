#pragma once
#include <string>

namespace nemo {
    class CryptoManager {
    public:
        CryptoManager();
        ~CryptoManager();

        bool initialize();
        std::string getCertificate() const;
        std::string getPrivateKey() const;

    private:
        std::string certificate_;
        std::string private_key_;
        
        bool generateKeysAndCertificate();
    };
}