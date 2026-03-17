"""
Minimal core module (project heart).
- Configures logging via env var.
- Exposes the C++ bindings module.
"""

import os
import sys


_LOG_ENV = "CORE_LOG"
_LOG_ENV_AASDK = "AASDK_CORE_LOG"
_BUILD_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build"))
if _BUILD_DIR not in sys.path:
    sys.path.insert(0, _BUILD_DIR)
_PYTHON_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "aasdk_proto"))
if _PYTHON_DIR not in sys.path:
    sys.path.insert(0, _PYTHON_DIR)


def _is_truthy(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on"}

io_context = __import__("io_context")
aasdk_logging = __import__("aasdk_logging")
cryptor = __import__("cryptor")
aasdk_usb = __import__("aasdk_usb")
transport = __import__("transport")
messenger = __import__("messenger")
channels = __import__("channels")
event = __import__("event")


def init_logging():
    try:
        from app.core import py_logging
        level_name = py_logging.configure_from_env()
    except Exception:
        level_name = "INFO"

    if _is_truthy(os.getenv(_LOG_ENV_AASDK, "0")):
        if hasattr(aasdk_logging, "set_aasdk_log_level"):
            try:
                aasdk_logging.set_aasdk_log_level(os.getenv(_LOG_ENV_AASDK, "0"))
            except Exception:
                pass
        elif hasattr(aasdk_logging, "configure_aasdk_logging"):
            try:
                aasdk_logging.configure_aasdk_logging(os.getenv(_LOG_ENV_AASDK, "0"), os.getenv(_LOG_ENV_AASDK, "0"), os.getenv(_LOG_ENV_AASDK, "0"))
            except Exception:
                pass
        elif hasattr(aasdk_logging, "enable_aasdk_logging"):
            if os.getenv(_LOG_ENV_AASDK, "0") in {"TRACE", "DEBUG"}:
                aasdk_logging.enable_aasdk_logging()


if _is_truthy(os.getenv(_LOG_ENV, "0")) or _is_truthy(os.getenv(_LOG_ENV_AASDK, "0")):
    init_logging()


__all__ = [
    "io_context",
    "init_logging",
    "cryptor",
    "aasdk_usb",
    "transport",
    "messenger",
    "channels",
    "event",
    "_LOG_ENV",
    "_LOG_ENV_AASDK"
]
