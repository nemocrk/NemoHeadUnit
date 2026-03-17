# POC Refactor Checklist (Orchestrator + Modular ChannelManager)

Goal: keep `POC` self‑contained while making the flow modular, explicit, and safe.
Avoid opening/announcing channels that have no handler.

## 1) Split Orchestration out of `POC/app/main.py`
- [ ] Create a dedicated module, e.g. `POC/app/orchestrator.py`.
- [ ] Move all wiring logic (runner, USB, transport, channel manager) out of `main.py`.
- [ ] `main.py` becomes a thin entrypoint that calls the orchestrator.

## 2) Define Clear, Self‑Explanatory Orchestrator Methods
Create explicit methods (naming can be adjusted, but keep clarity):
- [ ] `start_runner()`
- [ ] `start_usb()`
- [ ] `create_on_device_handler()`
- [ ] `create_transport_stack()`
- [ ] `create_channel_manager()`
- [ ] `start_channels()`
- [ ] `run_forever()`
- [ ] `stop()`

## 3) Orchestrator Owns Handler Creation
- [ ] Orchestrator is responsible for importing/creating handler logic instances.
- [ ] Orchestrator passes handlers into `ChannelManager` rather than `ChannelManager` creating them.
- [ ] This keeps channel ownership centralized and explicit.

Example (intended flow):
1. Orchestrator creates `ControlEventHandlerLogic`, `AudioEventHandlerLogic`, etc.
2. Orchestrator passes those into `ChannelManager`.
3. `ChannelManager` only instantiates channels for handlers it receives.

## 4) Modular `ChannelManager`
- [ ] Update `ChannelManager` to accept a dictionary or structured config of handlers.
- [ ] Only create channels for the handlers provided.

Suggested handler input pattern:
- [ ] `handlers = { "control": control_logic, "video": video_logic, ... }`
- [ ] `ChannelManager( ..., handlers=handlers )`

## 5) Control Handler Announces Only Available Channels
- [ ] Modify `ControlEventHandlerLogic` (or its orchestrator) to accept a list/set of enabled channels.
- [ ] Service discovery response should list only channels actually instantiated.
- [ ] `ChannelManager` should compute the enabled set based on provided handlers and pass it to control logic.

## 6) Channel Discovery / IDs in One Place
- [ ] Define a single authoritative mapping of channel IDs in one module.
- [ ] Use it consistently for both service discovery and channel creation.

## 7) Entry Point Simplification
`POC/app/main.py` should be reduced to:
- [ ] Create orchestrator instance
- [ ] `orchestrator.start()` or explicit `start_runner`/`start_usb`
- [ ] `orchestrator.run_forever()`
- [ ] Handle `KeyboardInterrupt` to call `orchestrator.stop()`

## 8) Optional Quality of Life
- [ ] Add a minimal logger wrapper for POC (so logs are consistent).
- [ ] Add a simple health check method (e.g., `orchestrator.is_running()`).

---

## Deliverables Summary
- New module: `POC/app/orchestrator.py`
- Updated: `POC/app/main.py` (thin entrypoint)
- Updated: `POC/app/core/channel_manager.py` (modular handlers)
- Updated: `POC/handlers/control/control_event_handler.py` or `service_discovery.py` to accept `enabled_channels`

