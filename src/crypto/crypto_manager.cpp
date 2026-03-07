#include "crypto_manager.hpp"
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <iostream>

namespace nemo {

CryptoManager::CryptoManager() {}
CryptoManager::~CryptoManager() {}

bool CryptoManager::initialize() {
    return generateKeysAndCertificate();
}

std::string CryptoManager::getCertificate() const { return certificate_; }
std::string CryptoManager::getPrivateKey() const { return private_key_; }

bool CryptoManager::generateKeysAndCertificate() {
    // Usa la API moderna (OpenSSL 3.0+) per la generazione delle chiavi
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) return false;
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY *pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Generazione del Certificato self-signed
    X509* x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 anno di validità
    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC, (unsigned char*)"NemoHeadUnit", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"NemoEmulator", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    if (!X509_sign(x509, pkey, EVP_sha256())) {
        X509_free(x509); 
        EVP_PKEY_free(pkey); 
        return false;
    }

    // Scrittura Private Key in memoria
    BIO* pkey_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(pkey_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    int pkey_len = BIO_pending(pkey_bio);
    private_key_.resize(pkey_len);
    BIO_read(pkey_bio, &private_key_[0], pkey_len);
    BIO_free(pkey_bio);

    // Scrittura Certificate in memoria
    BIO* cert_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(cert_bio, x509);
    int cert_len = BIO_pending(cert_bio);
    certificate_.resize(cert_len);
    BIO_read(cert_bio, &certificate_[0], cert_len);
    BIO_free(cert_bio);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    return true;
}

}