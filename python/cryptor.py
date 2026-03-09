from aasdk_interface import Cryptor
import ssl

class PythonCryptor(Cryptor):
    def __init__(self):
        super().__init__()
        print("[PythonCryptor] Inizializzato (dummy)")
        self.ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        self.ssl_context.check_hostname = False
        self.ssl_context.verify_mode = ssl.CERT_NONE

    def do_handshake(self, data: bytes) -> bytes:
        print("[PythonCryptor] WARNING: do_handshake non dovrebbe essere chiamato. Uso AASDK nativo.")
        return b""

    def encrypt(self, data: bytes) -> bytes:
        return data

    def decrypt(self, data: bytes) -> bytes:
        return data
