# POC Dependency Map

Scope: `POC/` only. This maps module-to-module dependencies as used by the Python POC and the C++ binding helpers.

Legend:
- `A -> B` means A imports/uses B directly.
- Grouped by layer to make the runtime flow readable.

## Runtime Flow (High Level)
- `POC/app/main.py -> POC/app/nemo_core.py`
- `POC/app/main.py -> POC/app/core/usb_hub_manager.py`
- `POC/app/main.py -> POC/app/core/transport_stack.py`
- `POC/app/main.py -> POC/app/core/channel_manager.py`
- `POC/app/core/channel_manager.py -> POC/handlers/control/control_event_handler.py`
- `POC/app/core/channel_manager.py -> POC/handlers/audio/audio_event_handler.py`

## App Layer
- `POC/app/main.py -> POC/app/nemo_core.py`
- `POC/app/main.py -> POC/app/core/usb_hub_manager.py`
- `POC/app/main.py -> POC/app/core/transport_stack.py`
- `POC/app/main.py -> POC/app/core/channel_manager.py`

- `POC/app/nemo_core.py -> build/io_context (pybind)`
- `POC/app/nemo_core.py -> build/logging (pybind)`
- `POC/app/nemo_core.py -> build/cryptor (pybind)`
- `POC/app/nemo_core.py -> build/aasdk_usb (pybind)`
- `POC/app/nemo_core.py -> build/transport (pybind)`
- `POC/app/nemo_core.py -> build/messenger (pybind)`
- `POC/app/nemo_core.py -> build/channels (pybind)`
- `POC/app/nemo_core.py -> build/event (pybind)`

- `POC/app/core/usb_hub_manager.py -> aasdk_usb (pybind)`
- `POC/app/core/transport_stack.py -> transport (pybind)`
- `POC/app/core/transport_stack.py -> messenger (pybind)`
- `POC/app/core/channel_manager.py -> channels (pybind)`
- `POC/app/core/channel_manager.py -> event (pybind)`
- `POC/app/core/channel_manager.py -> control_event (pybind)`
- `POC/app/core/channel_manager.py -> audio_event (pybind)`
- `POC/app/core/channel_manager.py -> POC/handlers/control/control_event_handler.py`
- `POC/app/core/channel_manager.py -> POC/handlers/audio/audio_event_handler.py`

## Control Handler Layer
- `POC/handlers/control/control_event_handler.py -> protobuf (pybind wrapper)`
- `POC/handlers/control/control_event_handler.py -> POC/handlers/control/control_modules/proto.py`
- `POC/handlers/control/control_event_handler.py -> POC/handlers/control/control_modules/handshake.py`
- `POC/handlers/control/control_event_handler.py -> POC/handlers/control/control_modules/service_discovery.py`
- `POC/handlers/control/control_event_handler.py -> POC/handlers/control/control_modules/focus.py`

- `POC/handlers/control/control_modules/handshake.py -> POC/handlers/control/control_modules/utils.py`
- `POC/handlers/control/control_modules/handshake.py -> POC/handlers/control/control_modules/proto.py`

- `POC/handlers/control/control_modules/service_discovery.py -> POC/handlers/control/control_modules/constants.py`
- `POC/handlers/control/control_modules/service_discovery.py -> POC/handlers/control/control_modules/utils.py`
- `POC/handlers/control/control_modules/service_discovery.py -> POC/handlers/control/control_modules/proto.py`

- `POC/handlers/control/control_modules/focus.py -> POC/handlers/control/control_modules/utils.py`
- `POC/handlers/control/control_modules/focus.py -> POC/handlers/control/control_modules/proto.py`

- `POC/handlers/control/control_modules/proto.py -> google.protobuf`
- `POC/handlers/control/control_modules/proto.py -> aasdk_proto.* (pb2 modules)`

## Audio Handler Layer
- `POC/handlers/audio/audio_event_handler.py -> protobuf (pybind wrapper)`
- `POC/handlers/audio/audio_event_handler.py -> google.protobuf`
- `POC/handlers/audio/audio_event_handler.py -> aasdk_proto.* (pb2 modules)`

## C++ Binding Helpers (header-only)
- `POC/handlers/common/event_binding.hpp -> pybind11`
- `POC/handlers/common/event_binding.hpp -> boost::asio`
- `POC/handlers/common/event_handler_utils.hpp -> aasdk::channel::SendPromise`
- `POC/handlers/common/event_handler_utils.hpp -> POC/handlers/common/protobuf_message.hpp`
- `POC/handlers/common/protobuf_message.hpp -> google::protobuf`

## Tests
- `POC/tests/test_poc.py -> build/protobuf (pybind)`
- `POC/tests/test_poc.py -> build/event (pybind)`
- `POC/tests/test_poc.py -> build/audio_event (pybind)`

## Notes
- `protobuf` in Python refers to the C++ pybind module exposing `GetProtobuf(...)` and `ProtobufMessage`.
- `aasdk_proto.*` are generated pb2 modules (Python). They are optional at runtime but required for control/audio logic to construct messages.
- The POC assumes the pybind modules are discoverable via the `build/` directory in `sys.path` (see `POC/app/nemo_core.py`).
