"""
Transport stack.
Creates USBTransport -> SSLWrapper/Cryptor -> MessageIn/Out -> Messenger.
Certificate/key are optional but required for TLS handshake.
"""

import os

try:
    import transport as _transport
    import messenger as _messenger
except Exception as e:
    raise ImportError("Native modules 'transport' and 'messenger' not available.") from e


def _ensure_cert_dir(path: str) -> None:
    if not os.path.isdir(path):
        os.makedirs(path, exist_ok=True)


def _write_certificates(cert_pem: str, key_pem: str, cert_dir: str) -> None:
    _ensure_cert_dir(cert_dir)
    with open(os.path.join(cert_dir, "headunit.crt"), "w", encoding="utf-8") as f:
        f.write(cert_pem)
    with open(os.path.join(cert_dir, "headunit.key"), "w", encoding="utf-8") as f:
        f.write(key_pem)


class TransportStack:
    def __init__(
        self,
        io_context_ptr: int,
        aoap_device,
    ):
        self._io_context_ptr = io_context_ptr
        self._aoap_device = aoap_device

        self.transport = _transport.USBTransport(self._io_context_ptr, self._aoap_device)
        self.ssl_wrapper = _transport.SSLWrapper()
        self.cryptor = _messenger.Cryptor(self.ssl_wrapper)

        self.cryptor.init()

        self.message_in = _messenger.MessageInStream(self._io_context_ptr, self.transport, self.cryptor)
        self.message_out = _messenger.MessageOutStream(self._io_context_ptr, self.transport, self.cryptor)
        self.messenger = _messenger.Messenger(self._io_context_ptr, self.message_in, self.message_out)

    def stop(self):
        if hasattr(self, "messenger") and self.messenger:
            self.messenger.stop()
        if hasattr(self, "transport") and self.transport:
            self.transport.stop()
        if hasattr(self, "cryptor") and self.cryptor:
            self.cryptor.deinit()
