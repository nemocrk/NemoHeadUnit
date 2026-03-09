#include "crypto_manager.hpp"
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <vector>

namespace nemo
{

    CryptoManager::CryptoManager() {}
    CryptoManager::~CryptoManager() {}

    bool CryptoManager::initialize()
    {
        return loadKeysAndCertificateFromFile();
    }

    std::string CryptoManager::getCertificate() const { return certificate_; }
    std::string CryptoManager::getPrivateKey() const { return private_key_; }

    // Cerca il file nei path standard (stessa logica di aasdk::Cryptor)
    static std::string findFile(const std::vector<std::string> &paths)
    {
        for (const auto &p : paths)
        {
            if (std::filesystem::exists(p))
            {
                std::ifstream f(p);
                if (f.good())
                {
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    std::cout << "[CryptoManager] Loaded from: " << p << std::endl;
                    return ss.str();
                }
            }
        }
        return {};
    }

    bool CryptoManager::loadKeysAndCertificateFromFile()
    {
        // Stessi path di aasdk::Cryptor (vedi log di avvio)
        const std::vector<std::string> certPaths = {
            "/etc/openauto/headunit.crt",
            "/usr/share/aasdk/cert/headunit.crt",
            "./cert/headunit.crt"};
        const std::vector<std::string> keyPaths = {
            "/etc/openauto/headunit.key",
            "/usr/share/aasdk/cert/headunit.key",
            "./cert/headunit.key"};

        certificate_ = findFile(certPaths);
        if (certificate_.empty())
        {
            std::cerr << "[CryptoManager] ERRORE: nessun certificato trovato!" << std::endl;
            return false;
        }

        private_key_ = findFile(keyPaths);
        if (private_key_.empty())
        {
            std::cerr << "[CryptoManager] ERRORE: nessuna chiave privata trovata!" << std::endl;
            return false;
        }

        // Validazione rapida: cert e chiave devono corrispondersi
        BIO *certBio = BIO_new_mem_buf(certificate_.c_str(), -1);
        X509 *x509 = PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr);
        BIO_free(certBio);

        BIO *keyBio = BIO_new_mem_buf(private_key_.c_str(), -1);
        EVP_PKEY *pk = PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr);
        BIO_free(keyBio);

        if (!x509 || !pk)
        {
            std::cerr << "[CryptoManager] ERRORE: parsing PEM fallito!" << std::endl;
            if (x509)
                X509_free(x509);
            if (pk)
                EVP_PKEY_free(pk);
            return false;
        }

        // Verifica che la chiave corrisponda al certificato
        if (X509_check_private_key(x509, pk) != 1)
        {
            std::cerr << "[CryptoManager] ERRORE: chiave e certificato NON corrispondono!" << std::endl;
            X509_free(x509);
            EVP_PKEY_free(pk);
            return false;
        }

        std::cout << "[CryptoManager] Certificato e chiave validati con successo." << std::endl;

        X509_free(x509);
        EVP_PKEY_free(pk);
        return true;
    }

} // namespace nemo
