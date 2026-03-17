"""
Python logging helpers with TRACE level and env-driven configuration.
"""

from __future__ import annotations

import os
import sysconfig
import importlib.util
import os.path
import logging as _maybe_logging

_LOG_ENV = "CORE_LOG"

if hasattr(_maybe_logging, "basicConfig"):
    py_logging = _maybe_logging
else:
    _stdlib = sysconfig.get_paths().get("stdlib")
    _logging_path = os.path.join(_stdlib, "logging", "__init__.py")
    _spec = importlib.util.spec_from_file_location("_stdlib_logging", _logging_path)
    _module = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_module)
    py_logging = _module

TRACE_LEVEL = 5

if not hasattr(py_logging, "TRACE"):
    py_logging.addLevelName(TRACE_LEVEL, "TRACE")


def _parse_level(value: str) -> int:
    v = (value or "").strip().lower()
    if v in {"trace", "verbose", "2"}:
        return TRACE_LEVEL
    if v in {"1", "true", "yes", "on", "debug"}:
        return py_logging.DEBUG
    if v in {"info"}:
        return py_logging.INFO
    if v in {"warning", "warn"}:
        return py_logging.WARNING
    if v in {"error"}:
        return py_logging.ERROR
    return py_logging.INFO


def _level_name(level: int) -> str:
    if level <= TRACE_LEVEL:
        return "TRACE"
    if level <= py_logging.DEBUG:
        return "DEBUG"
    if level <= py_logging.INFO:
        return "INFO"
    if level <= py_logging.WARNING:
        return "WARN"
    return "ERROR"


_CONFIGURED = False
_LEVEL_NAME = "INFO"


def _ensure_configured():
    global _CONFIGURED
    if _CONFIGURED:
        return
    level = _parse_level(os.getenv(_LOG_ENV, ""))
    global _LEVEL_NAME
    _LEVEL_NAME = _level_name(level)
    py_logging.basicConfig(
        level=level,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    _CONFIGURED = True


class _LoggerAdapter:
    def __init__(self, logger: py_logging.Logger):
        self._logger = logger

    def trace(self, msg: str, *args, **kwargs):
        if self._logger.isEnabledFor(TRACE_LEVEL):
            self._logger.log(TRACE_LEVEL, msg, *args, **kwargs)

    def debug(self, msg: str, *args, **kwargs):
        self._logger.debug(msg, *args, **kwargs)

    def info(self, msg: str, *args, **kwargs):
        self._logger.info(msg, *args, **kwargs)

    def warning(self, msg: str, *args, **kwargs):
        self._logger.warning(msg, *args, **kwargs)

    def error(self, msg: str, *args, **kwargs):
        self._logger.error(msg, *args, **kwargs)

    def is_enabled_for(self, level: int) -> bool:
        return self._logger.isEnabledFor(level)


_def_loggers = {}


def get_logger(name: str) -> _LoggerAdapter:
    _ensure_configured()
    if name not in _def_loggers:
        _def_loggers[name] = _LoggerAdapter(py_logging.getLogger(name))
    return _def_loggers[name]


def configure_from_env() -> str:
    _ensure_configured()
    return _LEVEL_NAME


def get_env_level_name() -> str:
    _ensure_configured()
    return _LEVEL_NAME
