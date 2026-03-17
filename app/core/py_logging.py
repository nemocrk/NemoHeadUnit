"""
Python logging helpers with TRACE level and env-driven configuration.
"""

from __future__ import annotations

import os
import sysconfig
import importlib.util
import os.path
import logging as _maybe_logging

try:
    from aasdk_bindings import aasdk_logging
except ImportError:
    aasdk_logging = None


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


def _aasdk_level_to_python(level: int) -> int:
    """Map aasdk::common::LogLevel (enum int) into Python logging levels."""

    # AASDK LogLevel values are generally: TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, FATAL=5
    try:
        lvl = int(level)
    except Exception:
        return py_logging.INFO

    if lvl <= 0:
        return TRACE_LEVEL
    if lvl == 1:
        return py_logging.DEBUG
    if lvl == 2:
        return py_logging.INFO
    if lvl == 3:
        return py_logging.WARNING
    if lvl == 4:
        return py_logging.ERROR
    return py_logging.CRITICAL


def _normalize_level(level: int | str) -> int:
    """Normalize a level passed from either Python or C++."""

    if isinstance(level, str):
        return _parse_level(level)
    try:
        lvl = int(level)
    except Exception:
        return py_logging.INFO

    # If it looks like an AASDK level (0..5), map it to Python.
    if 0 <= lvl <= 5:
        return _aasdk_level_to_python(lvl)

    return lvl


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
_MODULE_LEVELS: dict[str, int] = {}
_cpp_log_handler = None  # type: ignore[assignment]


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

    # If the C++ bindings are available, bridge the C++ logger to this one.
    # This allows Python-side configuration (e.g. module levels) to apply
    # to logs that originate in C++ land.
    if aasdk_logging:
        aasdk_logging.set_cpp_should_log_handler(should_log)
        aasdk_logging.set_cpp_log_handler(log_from_cpp)

    _CONFIGURED = True


def _get_effective_level(name: str) -> int:
    """Return the configured level for the given module name.

    Uses longest-prefix match (e.g. "app" matches "app.ui" etc.).
    Falls back to the root logger level.
    """

    if not name:
        return py_logging.getLogger().level

    parts = name.split(".")
    for i in range(len(parts), 0, -1):
        prefix = ".".join(parts[:i])
        if prefix in _MODULE_LEVELS:
            return _MODULE_LEVELS[prefix]

    return py_logging.getLogger().level


def _apply_module_level(name: str) -> None:
    """Ensure a logger has the configured level for the given module."""

    if not name:
        return

    level = _get_effective_level(name)
    logger = py_logging.getLogger(name)
    logger.setLevel(level)


def set_module_level(name: str, level: str | int) -> None:
    """Set the log level for a specific module name.

    The module name should follow the hierarchical pattern used by Python's
    logging (e.g. "app", "app.ui", "app.ui.android_auto").
    """

    _ensure_configured()

    level_int = _normalize_level(level)

    if not name:
        # Root logger canonical configuration.
        py_logging.getLogger().setLevel(level_int)
        _MODULE_LEVELS[""] = level_int
        return

    _MODULE_LEVELS[name] = level_int
    _apply_module_level(name)


def set_module_levels(levels: dict[str, str | int]) -> None:
    """Set multiple module levels at once."""

    for k, v in levels.items():
        set_module_level(k, v)


def get_configured_levels() -> dict[str, int]:
    """Return a copy of the configured module levels."""

    return dict(_MODULE_LEVELS)


def should_log(name: str, level: int | str) -> bool:
    """Query whether a message at `level` for `name` should be logged."""

    level_int = _normalize_level(level)
    effective = _get_effective_level(name)
    return int(level_int) >= int(effective)


def register_cpp_log_handler(handler):
    """Register a callback that receives log events from C++.

    The callback is called as `handler(name: str, level: int, message: str)`.
    """

    global _cpp_log_handler
    if handler is None:
        _cpp_log_handler = None
        return

    if not callable(handler):
        raise TypeError("handler must be callable")

    _cpp_log_handler = handler


def _dispatch_cpp_log(name: str, level: int, message: str) -> None:
    if _cpp_log_handler is None:
        return

    try:
        _cpp_log_handler(name, level, message)
    except Exception:
        # Avoid raising from C++ log paths.
        pass


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

    # Ensure that the logger reflects the configured module-level log settings.
    _apply_module_level(name)

    return _def_loggers[name]


def log_from_cpp(name: str, level: int, message: str) -> None:
    """Log an entry coming from C++.

    This is intended to be called from C++ bindings. It uses the same module
    level configuration as regular Python loggers.
    """

    python_level = _normalize_level(level)
    if not should_log(name, python_level):
        return

    logger = get_logger(name)
    try:
        logger._logger.log(python_level, message)
    except Exception:
        # Ensure C++ callers don't crash due to Python logging issues.
        pass


def apply_config(config) -> None:
    """Apply structured logging configuration.

    Expected to be passed a config object with:
      - core_level: str/int
      - core_module_levels: dict[str, str/int]
    """

    try:
        set_module_level("", getattr(config, "core_level", _LEVEL_NAME))
    except Exception:
        pass

    try:
        set_module_levels(getattr(config, "core_module_levels", {}))
    except Exception:
        pass


def configure_from_env() -> str:
    _ensure_configured()
    return _LEVEL_NAME


def get_env_level_name() -> str:
    _ensure_configured()
    return _LEVEL_NAME
