from .utils import log_and_send
from . import proto
from app.core.py_logging import get_logger

_logger = get_logger("app.control.handshake")


class HandshakeState:
    def __init__(self):
        if not proto.PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {proto.PROTOBUF_ERROR}")
        self.cryptor = None
        self.handshake_done = False

    def set_cryptor(self, cryptor):
        _logger.debug("Cryptor inizializzato dal C++")
        self.cryptor = cryptor

    def _drain_tls_out(self, max_iters: int = 32) -> bytes:
        out = b""
        for _ in range(max_iters):
            chunk = self.cryptor.read_handshake_buffer()
            if not chunk:
                break
            out += chunk
        return out

    def _step_handshake_and_collect(self, max_steps: int = 32):
        done = False
        out = b""
        for _ in range(max_steps):
            done = bool(self.cryptor.do_handshake())
            drained = self._drain_tls_out()
            if drained:
                out += drained
                continue
            break
        return done, out

    def on_version_response(self, major: int, minor: int, status: int) -> bytes:
        _logger.debug("VersionResponse ricevuta: %s.%s (status=%s)", major, minor, status)
        if status != 0:
            raise RuntimeError(f"Version negotiation fallita (status={status})")
        done, out = self._step_handshake_and_collect()
        return log_and_send("Invia TLS Flight 1 (drain)", out)

    def on_handshake(self, payload: bytes) -> bytes:
        _logger.debug("Handshake payload ricevuto (%d bytes)", len(payload))
        if payload:
            self.cryptor.write_handshake_buffer(payload)
        done, out = self._step_handshake_and_collect()
        if not done:
            return log_and_send("Invia TLS chunk (drain)", out)
        _logger.info("Handshake TLS completato")
        self.handshake_done = True
        if out:
            return log_and_send("Invia TLS finale (drain)", out)
        return b""

    def get_auth_complete_response(self) -> bytes:
        _logger.debug("Costruzione AuthResponse")
        msg = proto.AuthResponse_pb2.AuthResponse()
        msg.status = 0  # STATUS_SUCCESS
        return log_and_send("Invia AuthCompleteResponse", msg.SerializeToString())
