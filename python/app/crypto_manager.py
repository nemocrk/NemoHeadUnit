"""
python/app/crypto_manager.py

Equivalente Python di src/crypto/crypto_manager.cpp.

Responsabilità:
  - Ricerca del certificato TLS e della chiave privata sui path standard
  - Validazione PEM (parsing X.509 + verifica corrispondenza chiave pubblica)
  - Nessun binding C++ richiesto: usa la libreria `cryptography` (PyCA)

Dipendenze:
  pip install cryptography

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
import logging
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


def _validate_cert_key_pair(cert_pem: str, key_pem: str) -> bool:
    """
    Verifica che la chiave privata corrisponda al certificato X.509.
    Equivalente di X509_check_private_key() usato in crypto_manager.cpp.

    Usa `cryptography` (PyCA) — nessun binding C++/OpenSSL diretto.
    """
    try:
        from cryptography import x509
        from cryptography.hazmat.primitives.serialization import (
            load_pem_private_key,
            Encoding,
            PublicFormat,
        )
        from cryptography.hazmat.backends import default_backend

        backend = default_backend()

        cert = x509.load_pem_x509_certificate(cert_pem.encode(), backend)
        key  = load_pem_private_key(key_pem.encode(), password=None, backend=backend)

        cert_pub_bytes = cert.public_key().public_bytes(
            Encoding.PEM, PublicFormat.SubjectPublicKeyInfo
        )
        key_pub_bytes = key.public_key().public_bytes(
            Encoding.PEM, PublicFormat.SubjectPublicKeyInfo
        )

        if cert_pub_bytes != key_pub_bytes:
            log.error("[CryptoManager] ERRORE: chiave e certificato NON corrispondono!")
            return False

        log.info("[CryptoManager] Certificato e chiave validati con successo.")
        return True

    except ImportError:
        # cryptography non installata: skip validazione, solo warning
        log.warning(
            "[CryptoManager] libreria `cryptography` non trovata: "
            "validazione PEM saltata. Installa con: pip install cryptography"
        )
        return True  # non bloccare il boot se la lib non c'è

    except Exception as e:
        log.error("[CryptoManager] ERRORE parsing PEM: %s", e)
        return False


class CryptoManager:
    """
    Carica e valida il certificato TLS e la chiave privata per aasdk.

    Equivalente Python completo di src/crypto/crypto_manager.cpp.
    Non richiede compilazione C++.

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
            log.error(
                "[CryptoManager] ERRORE: nessun certificato trovato nei path: %s",
                self._cert_paths,
            )
            return False

        key_pem = _find_file(self._key_paths)
        if not key_pem:
            log.error(
                "[CryptoManager] ERRORE: nessuna chiave privata trovata nei path: %s",
                self._key_paths,
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
