# Remaining Work (src/ + python/)

This document summarizes what is still missing based on the current content of `src/` and `python/`.
It focuses on runtime gaps, missing integrations, and incomplete flows.

## Scope
- C++ core runtime: `src/`
- Python runtime + tools/tests/UI: `python/`

## What Exists (High‑Level)
- End‑to‑end USB discovery, transport, TLS cryptor, messenger, session startup: `src/usb/usb_hub_manager.*`, `src/session/session_manager.hpp`
- Core channel handlers (Control, Video, Audio, Sensor, Input, Navigation) in C++: `src/session/*_event_handler.hpp`
- Python bridge for orchestrator callbacks: `src/python/py_orchestrator.hpp`
- Python TLS cert handling: `python/app/crypto_manager.py`
- Phase 4/5 interactive tests and UI for video render: `python/test_interactive_phase4.py`, `python/test_phase5_dump.py`, `python/ui/main_window.py`

## Missing or Incomplete Work

### 1) Production Python Orchestrator (central gap)
Current interactive logic lives in `python/test_interactive_phase4.py` and is not in a reusable module.
`PyOrchestrator` requires a full method surface and will throw if missing.

Missing:
- A production orchestrator module (e.g. `python/app/orchestrator.py`) that implements **all** methods expected by `src/python/py_orchestrator.hpp`.
- Consistent behavior across tests/UI/headless using that single module.

Required Python methods (from `src/python/py_orchestrator.hpp`):
- TLS: `set_cryptor`, `on_version_status`, `on_handshake`, `get_auth_complete_response`
- Control: `on_service_discovery_request`, `on_ping_request`, `on_audio_focus_request`, `on_navigation_focus_request`, `on_voice_session_request`, `on_battery_status_notification`
- Media handshake: `on_av_channel_setup_request`, `on_channel_open_request`, `on_video_focus_request`
- Video stream hooks: `on_video_channel_open_request`, `on_video_channel_start`, `on_video_channel_stop`
- Sensor/Input/Navigation: `on_sensor_channel_open_request`, `on_sensor_start_request`, `on_input_channel_open_request`, `on_key_binding_request`, `on_navigation_channel_open_request`, `on_navigation_status_update`, `on_navigation_turn_event`, `on_navigation_distance_event`
- Audio strict hooks: `on_audio_media_channel_setup_request`, `on_audio_channel_open_request`, `on_audio_channel_start`, `on_audio_channel_stop`
- Error hook: `on_channel_error`

Why it matters:
- `src/session/audio_event_handler.hpp` **always calls** `on_audio_media_channel_setup_request` and `on_audio_channel_open_request`. If missing, `PyOrchestrator` throws and breaks audio channel setup.

### 2) Audio Channel Session ID / ACK flow
- `src/session/audio_event_handler.hpp` sends media ACK using `session_id_`, but it is never updated from Start indication.
- TODO: parse `Start` and set `session_id_` so ACKs are valid, or change ACK policy.

Files:
- `src/session/audio_event_handler.hpp`

### 3) Input pipeline (key/touch -> InputReport)
- Input channel responds to `KeyBindingRequest`, but no code generates actual `InputReport` events.
- UI (`python/ui/main_window.py`) currently does not translate Qt input events into AA input messages.

Missing:
- Python input event capture (key/touch) -> `InputReport` serialization -> send via `InputSourceService`.

Files:
- `python/ui/main_window.py`
- `src/session/input_event_handler.hpp`

### 4) Sensor data beyond gating
- Current logic supports only minimal gating (DrivingStatus/NightMode) in tests.
- No scheduling/streaming of other sensor types (speed, GPS, etc.).

Files:
- `python/test_interactive_phase4.py`
- `src/session/sensor_event_handler.hpp`

### 5) Navigation data handling
- C++ handler forwards nav status/turn/distance to Python, but Python currently ignores them.
- Missing: real usage, storage, or forwarding to a UI/cluster.

Files:
- `src/session/navigation_event_handler.hpp`
- `src/python/py_orchestrator.hpp`

### 6) Media Audio output pipeline
- There is no audio playback pipeline for PCM data (media/guidance/system).
- No sink integration (ALSA/PulseAudio/etc.).

Files:
- `src/session/audio_event_handler.hpp`
- (No audio sink implementation exists in `src/`)

### 7) Microphone (media source) capture
- MIC channel is opened, but there is no PCM capture from a real mic.
- Missing: capture -> `MediaSource` pipeline for AA.

Files:
- `src/session/session_manager.hpp` (mic channel creation)
- (No capture path in `src/` or `python/`)

### 8) Service coverage gaps (channels not implemented)
Only these channels are implemented in `src/session`: Control, Video, Audio, Sensor, Input, Navigation.
Missing (no handlers/channels):
- Bluetooth, PhoneStatus, MediaBrowser, MediaPlaybackStatus, Radio, GenericNotification, VendorExtension, WifiProjection, etc.

Evidence:
- Many protobufs exist under `python/aasdk_proto/...` but there are no handlers in `src/session/`.

### 9) `python/test_headless.py` is out of sync
- It returns `None` on methods that `PyOrchestrator` requires to return bytes.
- Will break with current C++ expectations for handshake.

Files:
- `python/test_headless.py`
- `src/python/py_orchestrator.hpp`

### 10) POC vs Core integration
- POC bindings are intentionally not wired into the core module (`src/binding.cpp`).
- If POC is meant to be the default runtime path, it needs an integration plan.

Files:
- `src/binding.cpp`

## Suggested Next Steps (Minimal Path)
1. Create `python/app/orchestrator.py` implementing the full `PyOrchestrator` surface.
2. Update tests and UI to use the new orchestrator.
3. Fix audio session_id_ and ACK handling.
4. Add InputReport generation (at least key events) in the UI.
5. Decide which additional channels are needed and implement handlers incrementally.

