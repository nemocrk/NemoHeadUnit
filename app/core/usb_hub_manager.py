"""
USB Hub Manager (USB-only).
Recreates the USB discovery part of usb_hub_manager.cpp using aasdk_usb bindings.
Non-USB steps (transport, cryptor, session) are intentionally left to the caller.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import time

try:
    import aasdk_usb as _usb
except Exception as e:
    raise ImportError("Native module 'aasdk_usb' not available. Build the bindings.") from e

from app.core.py_logging import get_logger

_logger = get_logger("app.usb")


class UsbHubManager:
    def __init__(self, io_context_ptr: int, libusb_context=None, libusb_context_ptr: int | None = None):
        self._io_context_ptr = io_context_ptr
        if libusb_context_ptr is None:
            if libusb_context is None:
                libusb_context = _usb.LibusbContext(self._io_context_ptr)
                if not libusb_context.initialize():
                    raise RuntimeError("Failed to initialize LibusbContext")
            libusb_context_ptr = libusb_context.get_context_ptr()
        elif libusb_context is None:
            _logger.warning("libusb_context_ptr provided without LibusbContext polling")

        self._libusb_context = libusb_context
        self._libusb_context_ptr = libusb_context_ptr

        self._usb_wrapper = _usb.USBWrapper(self._libusb_context_ptr)
        self._query_factory = _usb.AccessoryModeQueryFactory(self._usb_wrapper, self._io_context_ptr)
        self._query_chain_factory = _usb.AccessoryModeQueryChainFactory(
            self._usb_wrapper, self._io_context_ptr, self._query_factory
        )
        self._usb_hub = _usb.USBHub(self._usb_wrapper, self._io_context_ptr, self._query_chain_factory)
        _logger.info("USB hub initialized")

    def start(self, on_device, on_error):
        """
        Start discovery. When a device is found, on_device(handle) is called.
        on_error(msg) is called on failure.
        """
        _logger.info("USB discovery starting (USBHub.start)")
        self._usb_hub.start(self._io_context_ptr, on_device, on_error)

    def stop(self):
        _logger.info("USB discovery stopping (USBHub.cancel)")
        self._usb_hub.cancel()

    def hard_teardown(self):
        """
        Aggressive teardown: stop hub/context and drop references so the next
        start recreates libusb objects from scratch.
        """
        try:
            self.stop()
        except Exception:
            pass
        if self._libusb_context is not None:
            try:
                _logger.info("USB stopping LibusbContext (hard teardown)")
                self._libusb_context.stop()
            except Exception:
                pass
        time.sleep(0.2)
        self._try_usbreset()
        self._usb_hub = None
        self._usb_wrapper = None
        self._query_factory = None
        self._query_chain_factory = None
        self._libusb_context = None
        self._libusb_context_ptr = None

    def _try_usbreset(self):
        """
        Best-effort USB reset for Android Accessory devices (vendor 18d1).
        Requires the 'usbreset' utility to be available.
        """
        usbreset_bin = shutil.which("usbreset")
        if not usbreset_bin:
            _logger.debug("usbreset not available on PATH; skipping USB reset")
            return

        def _lsusb_targets():
            try:
                out = subprocess.check_output(["lsusb"], text=True, stderr=subprocess.DEVNULL)
            except Exception:
                return []
            pattern = re.compile(r"Bus (\d{3}) Device (\d{3}): ID ([0-9a-fA-F]{4}):([0-9a-fA-F]{4})")
            found = []
            for line in out.splitlines():
                m = pattern.search(line)
                if not m:
                    continue
                bus, dev, vid, pid = m.group(1), m.group(2), m.group(3).lower(), m.group(4).lower()
                if vid == "18d1":
                    found.append((bus, dev, vid, pid))
            return found

        targets = _lsusb_targets()
        if not targets:
            _logger.debug("usbreset: no Android accessory devices found")
            return

        # First try direct vid:pid reset (matches how you run it manually).
        for attempt in range(1, 4):
            try:
                _logger.info("usbreset: resetting 18d1:2d00 attempt=%d", attempt)
                res = subprocess.run(
                    [usbreset_bin, "18d1:2d00"],
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                if res.returncode == 0:
                    return
                _logger.warning(
                    "usbreset failed for 18d1:2d00 rc=%d stderr=%s",
                    res.returncode,
                    (res.stderr or "").strip(),
                )
            except Exception as exc:
                _logger.warning("usbreset exception for 18d1:2d00: %s", exc)
            time.sleep(0.2)

        # Fallback: resolve current bus/dev each attempt (device number can change quickly).
        for attempt in range(1, 4):
            targets = _lsusb_targets()
            if not targets:
                _logger.debug("usbreset: no Android accessory devices found (attempt=%d)", attempt)
                time.sleep(0.2)
                continue
            for bus, dev, vid, pid in targets:
                dev_path = f"/dev/bus/usb/{bus}/{dev}"
                try:
                    _logger.info("usbreset: resetting %s (id=%s:%s) attempt=%d", dev_path, vid, pid, attempt)
                    res = subprocess.run(
                        [usbreset_bin, dev_path],
                        check=False,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True,
                    )
                    if res.returncode == 0:
                        return
                    _logger.warning(
                        "usbreset failed for %s rc=%d stderr=%s",
                        dev_path,
                        res.returncode,
                        (res.stderr or "").strip(),
                    )
                except Exception as exc:
                    _logger.warning("usbreset exception for %s: %s", dev_path, exc)
            time.sleep(0.2)

    def create_aoap_device(self, device_handle):
        """
        Create AOAPDevice from discovered handle.
        Further transport/messenger setup is up to the caller.
        """
        _logger.info("USB creating AOAPDevice")
        return _usb.AOAPDevice.create(self._usb_wrapper, self._io_context_ptr, device_handle)

    @property
    def usb_wrapper(self):
        return self._usb_wrapper

    @property
    def usb_hub(self):
        return self._usb_hub
