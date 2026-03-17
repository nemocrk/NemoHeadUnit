import binascii
from app.core.py_logging import get_logger, TRACE_LEVEL

_logger = get_logger("app.control")


def log_and_send(label: str, data: bytes) -> bytes:
    if _logger.is_enabled_for(TRACE_LEVEL):
        preview = ""
        if len(data) > 0:
            preview = binascii.hexlify(data[:32]).decode()
        _logger.trace(
            "Action=%s size=%d bytes hex=%s",
            label,
            len(data),
            preview,
        )
    return data
