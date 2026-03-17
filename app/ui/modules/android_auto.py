"""
Android Auto interaction page (video surface + minimal overlay).
"""

from PyQt6.QtCore import Qt, QPoint, QEvent
from PyQt6.QtWidgets import QWidget, QLabel, QVBoxLayout, QFrame
from PyQt6.QtGui import QFont, QEventPoint

from .base import UIModule
from app.core.py_logging import get_logger
try:
    from aasdk_proto.aap_protobuf.service.inputsource.message.PointerAction_pb2 import PointerAction
except Exception:
    PointerAction = None

_logger = get_logger("app.ui.android_auto")


class VideoSurface(QWidget):
    def __init__(self, parent=None, target_width=800, target_height=480):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        self.setAttribute(Qt.WidgetAttribute.WA_AcceptTouchEvents, True)
        if hasattr(Qt.WidgetAttribute, "WA_SynthesizeMouseForUnhandledTouchEvents"):
            self.setAttribute(Qt.WidgetAttribute.WA_SynthesizeMouseForUnhandledTouchEvents, False)
        self.setObjectName("VideoSurface")
        self._input_frontend = None
        self._input_enabled = False
        self._target_width = int(target_width)
        self._target_height = int(target_height)
        self._last_touch_ts = 0.0
        self._pointer_id_map = {}
        self._next_pointer_id = 0
        self._active_raw_ids = set()
        self._last_positions = {}

    def paintEngine(self):
        return None

    def get_window_id(self) -> int:
        return int(self.winId())

    def set_input_frontend(self, frontend):
        self._input_frontend = frontend

    def set_input_enabled(self, enabled: bool):
        self._input_enabled = bool(enabled)

    def set_target_size(self, width: int, height: int):
        self._target_width = max(1, int(width))
        self._target_height = max(1, int(height))

    def _map_to_target(self, pos):
        w = max(1, self.width())
        h = max(1, self.height())
        # Keep aspect ratio: compute rendered video rect inside widget.
        scale = min(w / self._target_width, h / self._target_height)
        video_w = self._target_width * scale
        video_h = self._target_height * scale
        off_x = (w - video_w) / 2.0
        off_y = (h - video_h) / 2.0

        px = pos.x()
        py = pos.y()
        if px < off_x or py < off_y or px >= (off_x + video_w) or py >= (off_y + video_h):
            return None

        x = int(round(((px - off_x) / video_w) * self._target_width))
        y = int(round(((py - off_y) / video_h) * self._target_height))
        # Clamp to target bounds
        if x < 0:
            x = 0
        elif x >= self._target_width:
            x = self._target_width - 1
        if y < 0:
            y = 0
        elif y >= self._target_height:
            y = self._target_height - 1
        return x, y

    def _log_touch(self, action, pos, sent_x, sent_y, pid=0, ignored=False):
        try:
            p = QPoint(int(pos.x()), int(pos.y()))
            global_pt = self.mapToGlobal(p)
            main = self.window()
            if main is not None:
                main_local = main.mapFromGlobal(global_pt)
                main_x, main_y = int(main_local.x()), int(main_local.y())
            else:
                main_x, main_y = -1, -1
            _logger.trace(
                "Touch %s pid=%d main=(%d,%d) widget=(%d,%d) sent=(%d,%d)%s",
                action,
                int(pid),
                main_x,
                main_y,
                int(pos.x()),
                int(pos.y()),
                int(sent_x),
                int(sent_y),
                " ignored" if ignored else "",
            )
        except Exception:
            _logger.trace(
                "Touch %s pid=%d sent=(%d,%d)%s",
                action,
                int(pid),
                int(sent_x),
                int(sent_y),
                " ignored" if ignored else "",
            )

    @staticmethod
    def _now():
        try:
            import time
            return time.monotonic()
        except Exception:
            return 0.0

    def _should_ignore_mouse(self, event):
        if (self._now() - self._last_touch_ts) < 0.2:
            return True
        try:
            source = event.source()
            if hasattr(Qt, "MouseEventSource"):
                if source in (
                    Qt.MouseEventSource.MouseEventSynthesizedBySystem,
                    Qt.MouseEventSource.MouseEventSynthesizedByQt,
                ):
                    return True
        except Exception:
            pass
        return False

    def mousePressEvent(self, event):
        if self._input_frontend is None or not self._input_enabled:
            return super().mousePressEvent(event)
        if self._should_ignore_mouse(event):
            return
        pos = event.position()
        mapped = self._map_to_target(pos)
        if mapped is None:
            self._log_touch("down", pos, -1, -1, 0, ignored=True)
            return
        x, y = mapped
        self._log_touch("down", pos, x, y, 0)
        self._input_frontend.send_touch_down(x, y, 0)

    def mouseReleaseEvent(self, event):
        if self._input_frontend is None or not self._input_enabled:
            return super().mouseReleaseEvent(event)
        if self._should_ignore_mouse(event):
            return
        pos = event.position()
        mapped = self._map_to_target(pos)
        if mapped is None:
            self._log_touch("up", pos, -1, -1, 0, ignored=True)
            return
        x, y = mapped
        self._log_touch("up", pos, x, y, 0)
        self._input_frontend.send_touch_up(x, y, 0)

    def mouseMoveEvent(self, event):
        if self._input_frontend is None or not self._input_enabled:
            return super().mouseMoveEvent(event)
        if self._should_ignore_mouse(event):
            return
        if event.buttons() == Qt.MouseButton.NoButton:
            return
        pos = event.position()
        mapped = self._map_to_target(pos)
        if mapped is None:
            self._log_touch("move", pos, -1, -1, 0, ignored=True)
            return
        x, y = mapped
        self._log_touch("move", pos, x, y, 0)
        self._input_frontend.send_touch_move(x, y, 0)

    def _handle_touch_points(self, points, event_type=None):
        if self._input_frontend is None or not self._input_enabled:
            return
        if PointerAction is None:
            return
        self._last_touch_ts = self._now()
        if not points:
            return

        try:
            for tp in points:
                _logger.trace(
                    "TouchRaw id=%s state=%s pos=(%d,%d)",
                    getattr(tp, "id", lambda: "?")(),
                    int(tp.state()) if hasattr(tp, "state") else -1,
                    int(tp.position().x()),
                    int(tp.position().y()),
                )
        except Exception:
            pass

        def _pid(tp):
            try:
                return int(tp.id())
            except Exception:
                return 0

        pre_active_count = len(self._active_raw_ids)
        force_press_all = bool(event_type == QEvent.Type.TouchBegin and pre_active_count == 0)
        force_release_all = bool(event_type == QEvent.Type.TouchEnd)
        saw_press = False
        for tp in points:
            try:
                if tp.state() & QEventPoint.State.Pressed:
                    saw_press = True
                    break
            except Exception:
                continue
        if pre_active_count == 0 and not saw_press:
            if event_type == QEvent.Type.TouchUpdate:
                force_press_all = True
        # New gesture: reset id mapping so pointer ids start from 0.
        if pre_active_count == 0 and (saw_press or force_press_all):
            self._pointer_id_map.clear()
            self._next_pointer_id = 0
            self._last_positions.clear()

        mapped_points = []
        action_index = None
        action = None
        has_pressed = False
        has_released = False
        has_moved = False
        changed_mapped_id = None

        for tp in points:
            pos = tp.position()
            pid = _pid(tp)
            state = tp.state()
            mapped = self._map_to_target(pos)
            pressed = bool(state & QEventPoint.State.Pressed) or force_press_all
            released = bool(state & QEventPoint.State.Released) or force_release_all
            moved = bool(state & QEventPoint.State.Updated)
            if pressed:
                self._active_raw_ids.add(pid)
            if released and pid in self._active_raw_ids:
                self._active_raw_ids.remove(pid)

            if mapped is None:
                if pressed or released or moved:
                    self._log_touch("touch", pos, -1, -1, pid, ignored=True)
                self._log_touch("touch", pos, -1, -1, pid, ignored=True)
                # Ignore points outside the video region.
                continue
            # Map raw pointer ids to small stable ids.
            mapped_id = self._pointer_id_map.get(pid)
            if mapped_id is None:
                mapped_id = self._next_pointer_id
                self._next_pointer_id += 1
                self._pointer_id_map[pid] = mapped_id

            x, y = mapped
            self._last_positions[pid] = (x, y)
            mapped_points.append((mapped_id, x, y))

            if pressed:
                has_pressed = True
                if changed_mapped_id is None:
                    changed_mapped_id = mapped_id
            elif released:
                has_released = True
                if changed_mapped_id is None:
                    changed_mapped_id = mapped_id
            elif moved:
                has_moved = True

            self._log_touch("touch", pos, x, y, mapped_id)

        # Stable ordering by mapped id.
        mapped_points.sort(key=lambda it: it[0])

        if has_pressed:
            action = PointerAction.ACTION_DOWN if pre_active_count == 0 else PointerAction.ACTION_POINTER_DOWN
        elif has_released:
            action = PointerAction.ACTION_UP if len(self._active_raw_ids) == 0 else PointerAction.ACTION_POINTER_UP
        elif has_moved:
            action = PointerAction.ACTION_MOVED
        else:
            return

        if not mapped_points:
            if action == PointerAction.ACTION_UP and len(self._active_raw_ids) == 0:
                self._pointer_id_map.clear()
                self._next_pointer_id = 0
                self._last_positions.clear()
            return

        if changed_mapped_id is not None:
            for idx, (mid, _x, _y) in enumerate(mapped_points):
                if mid == changed_mapped_id:
                    action_index = idx
                    break
        if action_index is None:
            action_index = 0

        _logger.trace(
            "TouchEvent points=%d action=%d action_index=%d",
            len(mapped_points),
            int(action),
            int(action_index),
        )
        # Send in (x, y, pointer_id) order expected by InputFrontend
        self._input_frontend.send_touch(
            action,
            [(x, y, mid) for (mid, x, y) in mapped_points],
            action_index,
        )
        # Reset mapping once all pointers are released (new gesture can start fresh).
        if action in (PointerAction.ACTION_UP, PointerAction.ACTION_POINTER_UP) and len(self._active_raw_ids) == 0:
            self._pointer_id_map.clear()
            self._next_pointer_id = 0
            self._last_positions.clear()
            self._active_raw_ids.clear()

    def _handle_touch_cancel(self):
        if self._input_frontend is None or not self._input_enabled:
            self._pointer_id_map.clear()
            self._next_pointer_id = 0
            self._last_positions.clear()
            self._active_raw_ids.clear()
            return
        if PointerAction is None:
            self._pointer_id_map.clear()
            self._next_pointer_id = 0
            self._last_positions.clear()
            self._active_raw_ids.clear()
            return
        if not self._active_raw_ids:
            self._pointer_id_map.clear()
            self._next_pointer_id = 0
            self._last_positions.clear()
            return
        pointers = []
        for raw_id in self._active_raw_ids:
            mapped_id = self._pointer_id_map.get(raw_id)
            pos = self._last_positions.get(raw_id)
            if mapped_id is None or pos is None:
                continue
            x, y = pos
            pointers.append((mapped_id, x, y))
        if pointers:
            pointers.sort(key=lambda it: it[0])
            self._input_frontend.send_touch(
                PointerAction.ACTION_UP,
                [(x, y, mid) for (mid, x, y) in pointers],
                0,
            )
        self._pointer_id_map.clear()
        self._next_pointer_id = 0
        self._last_positions.clear()
        self._active_raw_ids.clear()

    def event(self, event):
        if event.type() in (QEvent.Type.TouchBegin, QEvent.Type.TouchUpdate, QEvent.Type.TouchEnd):
            event.accept()
            if self._input_frontend is None or not self._input_enabled:
                return True
            points = event.points()
            self._handle_touch_points(points, event.type())
            return True
        if hasattr(QEvent.Type, "TouchCancel") and event.type() == QEvent.Type.TouchCancel:
            event.accept()
            self._handle_touch_cancel()
            return True
        return super().event(event)

    def touchEvent(self, event):
        if self._input_frontend is None or not self._input_enabled:
            return super().touchEvent(event)
        points = event.points()
        self._handle_touch_points(points, event.type())
        event.accept()


class AndroidAutoModule(UIModule):
    def __init__(self):
        super().__init__(name="Android Auto", region="page")
        self._video_widget = None

    def build(self, parent=None):
        root = QWidget(parent)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(16)

        title = QLabel("Android Auto")
        title.setObjectName("PageTitle")
        title.setFont(QFont("Montserrat", 20, QFont.Weight.DemiBold))

        frame = QFrame()
        frame.setObjectName("VideoFrame")
        frame_layout = QVBoxLayout(frame)
        frame_layout.setContentsMargins(0, 0, 0, 0)

        self._video_widget = VideoSurface(frame, target_width=800, target_height=480)
        frame_layout.addWidget(self._video_widget)

        hint = QLabel("Video stream ready")
        hint.setObjectName("MutedHint")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint.setFont(QFont("Montserrat", 11))

        layout.addWidget(title)
        layout.addWidget(frame, 1)
        layout.addWidget(hint)

        return root

    def get_video_widget(self) -> VideoSurface:
        return self._video_widget
