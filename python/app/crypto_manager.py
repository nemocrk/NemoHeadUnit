"""
python/app/crypto_manager.py

Equivalente Python di src/crypto/crypto_manager.cpp.

Responsabilità:
  - Ricerca del certificato TLS e della chiave privata sui path standard
  - Validazione PEM tramite `openssl` CLI di sistema (nessuna dipendenza Python)
  - La vera verifica TLS è comunque delegata a OpenSSL C++ dentro aasdk

Dipendenze:
  - /usr/bin/openssl  (presente su qualsiasi sistema Linux)
  - NESSUNA dipendenza Python esterna

Path cercati (stesso ordine di aasdk::Cryptor):
  Certificato : /etc/openauto/headunit.crt
                /usr/share/aasdk/cert/headunit.crt
                ./cert/headunit.crt
  Chiave priv.: /etc/openauto/headunit.key
                /usr/share/aasdk/cert/headunit.key
                ./cert/headunit.key

Uso:
    crypto = CryptoManager()
    if not crypto.initialize():
        raise RuntimeError("Certificati non trovati o non validi")
    usb.set_certificate_and_key(crypto.get_certificate(), crypto.get_private_key())
"""

from __future__ import annotations

import os
import sys
import shutil
import logging
import subprocess
import tempfile
from typing import Optional

log = logging.getLogger(__name__)

# Percorsi standard (allineati a aasdk::Cryptor e crypto_manager.cpp)
_CERT_PATHS: list[str] = [
    "/etc/openauto/headunit.crt",
    "/usr/share/aasdk/cert/headunit.crt",
    "./cert/headunit.crt",
]

_KEY_PATHS: list[str] = [
    "/etc/openauto/headunit.key",
    "/usr/share/aasdk/cert/headunit.key",
    "./cert/headunit.key",
]

# Individua openssl al momento dell'import: preferisce il sistema (/usr/bin)
# così non dipende dall'environment conda/venv attivo.
_OPENSSL_BIN: Optional[str] = (
    "/usr/bin/openssl"
    if os.path.isfile("/usr/bin/openssl")
    else shutil.which("openssl")
)

if _OPENSSL_BIN:
    log.debug("[CryptoManager] openssl trovato: %s", _OPENSSL_BIN)
else:
    log.warning("[CryptoManager] openssl CLI non trovato: validazione PEM disabilitata.")


def _find_file(paths: list[str]) -> Optional[str]:
    """Cerca il primo file esistente e leggibile tra i path forniti."""
    for p in paths:
        if os.path.isfile(p):
            try:
                with open(p, "r") as f:
                    content = f.read()
                log.info("[CryptoManager] Caricato: %s", p)
                return content
            except OSError as e:
                log.warning("[CryptoManager] Impossibile leggere %s: %s", p, e)
    return None


def _get_pubkey_from_cert(cert_pem: str) -> Optional[str]:
    """
    Estrae la chiave pubblica dal certificato X.509 tramite openssl CLI.
    Equivalente di: openssl x509 -pubkey -noout
    """
    if not _OPENSSL_BIN:
        return None
    try:
        result = subprocess.run(
            [_OPENSSL_BIN, "x509", "-pubkey", "-noout"],
            input=cert_pem.encode(),
            capture_output=True,
            timeout=5,
        )
        if result.returncode == 0:
            return result.stdout.decode()
        log.error("[CryptoManager] x509 pubkey extract failed: %s", result.stderr.decode())
        return None
    except Exception as e:
        log.error("[CryptoManager] Errore subprocess x509: %s", e)
        return None


def _get_pubkey_from_privkey(key_pem: str) -> Optional[str]:
    """
    Estrae la chiave pubblica dalla chiave privata tramite openssl CLI.
    Supporta RSA, EC, ed25519 (openssl pkey è universale).
    Equivalente di: openssl pkey -pubout
    """
    if not _OPENSSL_BIN:
        return None
    try:
        result = subprocess.run(
            [_OPENSSL_BIN, "pkey", "-pubout"],
            input=key_pem.encode(),
            capture_output=True,
            timeout=5,
        )
        if result.returncode == 0:
            return result.stdout.decode()
        log.error("[CryptoManager] pkey pubout failed: %s", result.stderr.decode())
        return None
    except Exception as e:
        log.error("[CryptoManager] Errore subprocess pkey: %s", e)
        return None


def _validate_cert_key_pair(cert_pem: str, key_pem: str) -> bool:
    """
    Verifica che la chiave privata corrisponda al certificato X.509.
    Strategia: estrae la chiave pubblica da entrambi e le confronta.
    Equivalente di X509_check_private_key() in crypto_manager.cpp.

    Usa esclusivamente `openssl` CLI di sistema — nessuna dipendenza Python.
    Se openssl non è disponibile, skip con warning (la vera verifica la fa C++).
    """
    if not _OPENSSL_BIN:
        log.warning(
            "[CryptoManager] openssl CLI non disponibile: validazione PEM saltata."
        )
        return True  # fallback permissivo: aasdk C++ verifica comunque

    cert_pub = _get_pubkey_from_cert(cert_pem)
    if cert_pub is None:
        log.error("[CryptoManager] Impossibile estrarre pubkey dal certificato.")
        return False

    key_pub = _get_pubkey_from_privkey(key_pem)
    if key_pub is None:
        log.error("[CryptoManager] Impossibile estrarre pubkey dalla chiave privata.")
        return False

    # Normalizza (strip whitespace) prima del confronto
    if cert_pub.strip() != key_pub.strip():
        log.error("[CryptoManager] ERRORE: chiave e certificato NON corrispondono!")
        print(
            "[CryptoManager] ERRORE CRITICO: cert e key non formano una coppia valida!",
            file=sys.stderr,
        )
        return False

    log.info("[CryptoManager] Certificato e chiave validati con successo (openssl CLI).")
    return True


class CryptoManager:
    """
    Carica e valida il certificato TLS e la chiave privata per aasdk.

    Equivalente Python completo di src/crypto/crypto_manager.cpp.
    Non richiede compilazione C++ ne' librerie Python esterne.

    Esempio d'uso:
        crypto = CryptoManager()
        if not crypto.initialize():
            sys.exit(1)
        usb.set_certificate_and_key(
            crypto.get_certificate(),
            crypto.get_private_key()
        )
    """

    def __init__(
        self,
        cert_paths: Optional[list[str]] = None,
        key_paths:  Optional[list[str]] = None,
    ):
        """
        Parametri opzionali per override dei path di default.
        Se non forniti, usa i path standard di aasdk.
        """
        self._cert_paths: list[str] = cert_paths or _CERT_PATHS
        self._key_paths:  list[str] = key_paths  or _KEY_PATHS
        self._certificate: str = ""
        self._private_key: str = ""

    # ------------------------------------------------------------------
    # API pubblica — stessa interfaccia di CryptoManager C++
    # ------------------------------------------------------------------

    def initialize(self) -> bool:
        """Carica e valida cert + key. Ritorna True se tutto ok."""
        cert_pem = _find_file(self._cert_paths)
        if not cert_pem:
            print(
                f"[CryptoManager] ERRORE: nessun certificato trovato.\n"
                f"  Path cercati: {self._cert_paths}",
                file=sys.stderr,
            )
            return False

        key_pem = _find_file(self._key_paths)
        if not key_pem:
            print(
                f"[CryptoManager] ERRORE: nessuna chiave privata trovata.\n"
                f"  Path cercati: {self._key_paths}",
                file=sys.stderr,
            )
            return False

        if not _validate_cert_key_pair(cert_pem, key_pem):
            return False

        self._certificate = cert_pem
        self._private_key = key_pem
        return True

    def get_certificate(self) -> str:
        """Ritorna il certificato PEM come stringa."""
        return self._certificate

    def get_private_key(self) -> str:
        """Ritorna la chiave privata PEM come stringa."""
        return self._private_key
