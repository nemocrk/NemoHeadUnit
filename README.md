# POC: strict Python-driven handler

This POC is an isolated sketch (not wired into build) showing the intended flow:

1. `AudioEventHandler` is a thin C++ pass-through.
2. Python receives `(payload_bytes, channel, strand)` and performs all logic.
3. Python uses C++ protobuf objects (via `GetProtobuf`) and channel methods.

Missing pieces to wire it in:
1. Bindings for `AudioChannel` and `StrandDispatcher`.
2. A pybind module that exposes `GetProtobuf` + channel methods.
3. Hooking this POC handler into `SessionManager` instead of the current one.

