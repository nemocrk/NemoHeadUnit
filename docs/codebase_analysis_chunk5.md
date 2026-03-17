# Codebase Analysis — Chunk 5
## Handler Layer Completo (29 file · Copertura totale: 67/67)

> **Prerequisiti:** Questo documento è il quinto e ultimo capitolo dell'analisi del codebase NemoHeadUnit.
> Chunk 1–4 coprono CMakeLists, app core, external_bindings, UI, control modules.
> Chunk 5 copre l'intero layer `handlers/` — tutti i canali AAP rimanenti.

---

## 1. Pattern Architetturale Universale — Dual-Layer C++/Python

Ogni handler del progetto è composto da **due file con ruoli nettamente separati**:

| File | Layer | Responsabilità |
|---|---|---|
| `handler_name.hpp` | C++ | Riceve eventi aasdk via vtable, invoca `CALL_PY()`, espone binding pybind11 del canale |
| `handler_name.py` | Python | `XxxOrchestrator` (logica pura) + `XxxEventHandlerLogic` (I/O protobuf + invio risposta) |

Il layer C++ **non prende mai decisioni di protocollo**: ogni override è un singolo `CALL_PY(message)`.
Tutta la logica AAP — parsing, costruzione risposte, gestione stato — vive in Python.

### 1.1 Struttura Python Standard

```python
class XxxOrchestrator:
    """
    Logica pura: riceve bytes, ritorna bytes.
    Sostituibile in unit-test senza mock del canale C++.
    """
    def on_channel_open_request(self, request_bytes) -> bytes: ...
    def on_xxx_specific_event(self, request_bytes) -> bytes: ...
    def on_channel_error(self, channel_name, error_str): return None


class XxxEventHandlerLogic:
    """
    I/O layer: estrae channel/strand dall'handler C++, chiama Orchestrator,
    serializza la risposta via core.GetProtobuf() e la invia sul canale.
    """
    def on_channel_open_request(self, handler, payload: bytes):
        channel = handler.channel
        strand  = handler.strand
        res_bytes = self._orchestrator.on_channel_open_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf....ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "Tag/ChannelOpenResponse")
        channel.receive(handler)  # ← INVARIANTE: sempre alla fine
```

### 1.2 Invariante `channel.receive(handler)`

La chiamata `channel.receive(handler)` **alla fine di ogni metodo** è il loop di ricezione aasdk.
Senza di essa il canale smette silenziosamente di ricevere messaggi — nessun errore, nessun log, solo silenzio.
È presente in ogni singolo handler del progetto senza eccezioni.

### 1.3 Classificazione degli Handler per Complessità

| Classe | Handler | Caratteristica |
|---|---|---|
| **Media Active** | `audio`, `video`, `media_source` | Hot-path diretto a `AvCore`, bypass GIL, thread dedicato |
| **Protocol Active** | `control`, `sensor`, `input`, `bluetooth`, `wifi_projection` | Logica di protocollo non triviale, risposte multi-messaggio |
| **Passive Sink** | `navigation`, `phone_status`, `media_browser`, `media_playback_status`, `radio`, `vendor_extension`, `generic_notification` | Aprono il canale, ricevono dati, hook disponibile via Orchestrator override |

---

## 2. `handlers/control/control_event_handler.hpp` — Control Channel Binding

Il `.hpp` del canale di controllo è il più ricco del progetto: espone **13 metodi `send_*`** sulla classe `ControlChannel`,
riflettendo la complessità del protocollo di controllo AAP (handshake TLS, service discovery, focus, ping, bye-bye).

### 2.1 Metodi `ControlChannel` esposti a Python

| Metodo pybind11 | Messaggio protobuf |
|---|---|
| `send_version_request` | — (nessun payload) |
| `send_handshake` | `py::bytes` raw (TLS) |
| `send_auth_complete` | `AuthResponse` |
| `send_service_discovery_response` | `ServiceDiscoveryResponse` |
| `send_audio_focus_response` | `AudioFocusNotification` |
| `send_shutdown_request` | `ByeByeRequest` |
| `send_shutdown_response` | `ByeByeResponse` |
| `send_navigation_focus_response` | `NavFocusNotification` |
| `send_voice_session_focus_response` | `VoiceSessionNotification` |
| `send_ping_request` | `PingRequest` |
| `send_ping_response` | `PingResponse` |

### 2.2 `onVersionResponse` — Serializzazione Custom

```cpp
void onVersionResponse(uint16_t major, uint16_t minor,
                       aap_protobuf::shared::MessageStatus status) override
{
    const std::string payload =
        std::to_string(major) + "|" +
        std::to_string(minor) + "|" +
        std::to_string(static_cast<int>(status));
    CALL_PY(payload);
}
```

È il **solo caso** in tutto il codebase dove `CALL_PY` riceve una stringa custom invece di un messaggio protobuf.
Motivo: `onVersionResponse` ha firma non-protobuf nell'interfaccia aasdk (tre interi separati).
Vengono serializzati manualmente con `|` come separatore e de-serializzati in Python con `payload.split("|")`
nel modulo `handlers/control/control_modules/handshake.py`.

### 2.3 `onHandshake` — Bytes Raw TLS

```cpp
void onHandshake(const aasdk::common::DataConstBuffer &payload) override
{
    const std::string data(
        reinterpret_cast<const char*>(payload.cdata), payload.size);
    CALL_PY(data);
}
```

Il payload TLS handshake è passato come `std::string` di bytes raw — il secondo e ultimo caso non-protobuf.
Coerente con `HandshakeState.on_handshake(payload: bytes)` in `handlers/control/control_modules/handshake.py`.

---

## 3. `handlers/audio/` — Audio Sink (GIL-Free Hot Path)

### 3.1 `audio_event_handler.hpp` — `pushAudio` Diretto a AvCore

```cpp
void onMediaWithTimestampIndication(
    aasdk::messenger::Timestamp::ValueType ts,
    const aasdk::common::DataConstBuffer &buffer) override
{
    if (av_core_) {
        av_core_->pushAudio(
            static_cast<int>(channel_->getId()),
            static_cast<uint64_t>(ts),
            buffer.cdata,
            buffer.size);
    }
    CALL_PY("");  // payload vuoto: solo notifica statistica a Python
}
```

Questo è il **punto critico GIL** del progetto: `pushAudio` è chiamato direttamente in C++ sull'`AvCore`,
bypassando completamente Python. Il buffer PCM grezzo non attraversa mai il GIL — zero-copy dal socket USB all'`AvCore`.
`CALL_PY("")` invia solo una notifica vuota a Python (per debug/statistiche), mai il payload audio.

### 3.2 `AvCore*` come `uintptr_t`

```cpp
AudioEventHandler(..., std::uintptr_t av_core_ptr = 0)
    : av_core_(reinterpret_cast<nemo::AvCore*>(av_core_ptr))
```

`AvCore*` è passato come intero (`uintptr_t`) attraverso il binding Python — necessario perché
`AvCore` non è esposto come classe pybind11 (sarebbe troppo costoso gestirne il lifecycle cross-GIL).
Python ottiene il puntatore via `av_core.get_ptr()` e lo passa al costruttore dell'handler.

### 3.3 `audio_event_handler.py` — `session_id` e `MediaAck`

```python
def on_media_channel_start_indication(self, handler, payload):
    start = MediaStart()
    start.ParseFromString(payload)
    self._orchestrator.set_audio_session_id(start.session_id)
    # ... channel.receive(handler)
```

Il `session_id` estratto da `MediaChannelStart` è obbligatorio per costruire i `MediaAck` successivi.
Senza il `session_id` corretto, gli ack sono ignorati dal telefono e la riproduzione si interrompe
dopo l'esaurimento del buffer iniziale.

### 3.4 Double-Parse per Debug TRACE

```python
# Primo parse: via binding C++ per inviare
ack = core.GetProtobuf("aap_protobuf.service.media.source.message.Ack")
ack.parse_from_string(ack_bytes)
channel.send_media_ack(ack, strand, tag)

# Secondo parse: via protobuf Python puro per log TRACE
try:
    ack_dbg = MediaAck()
    ack_dbg.ParseFromString(ack_bytes)
    _logger.trace("session_id=%d ack=%d", ack_dbg.session_id, ack_dbg.ack)
except Exception:
    pass  # mai interrompere il flusso audio per un errore di debug
```

Il double-parse è intenzionale: `core.GetProtobuf` usa il binding C++ (necessario per `send_media_ack`),
`MediaAck()` usa protobuf Python puro per estrarre i campi nel log TRACE. Il `try/except: pass` garantisce
che nessun errore di debug interrompa mai il flusso audio in produzione.

---

## 4. `handlers/video/` — Video Sink (VideoFocus Post-Setup)

### 4.1 `video_event_handler.py` — Callback `_after_setup`

```python
def on_media_channel_setup_request(self, handler, payload):
    # ...
    def _after_setup(err):
        if err:
            _logger.error("AVChannelSetupResponse FAILED: %s", err)
            return
        # Invia VideoFocusNotification DENTRO la callback then_cb
        vf_bytes = self._orchestrator.on_video_focus_post_setup(channel_id)
        vf = core.GetProtobuf("...VideoFocusNotification")
        vf.parse_from_string(vf_bytes)
        channel.send_video_focus_indication(vf, strand, tag)

    channel.send_channel_setup_response(cfg, strand, tag, _after_setup)
```

A differenza dell'`AudioHandler`, il video invia una `VideoFocusNotification(VIDEO_FOCUS_PROJECTED)`
**immediatamente dopo** la `AVChannelSetupResponse`, nella callback `then_cb` di `with_promise`.
Questo è necessario: il telefono deve ricevere il focus video **prima** di iniziare lo streaming H.264.
La sequenza `SetupResponse → FocusNotification` deve essere atomica sulla stessa strand.

### 4.2 `VideoOrchestrator.on_active_changed` — Hook UI

```python
class VideoOrchestrator:
    def __init__(self, on_active_changed=None):
        self._on_active_changed = on_active_changed

    def on_media_channel_start(self, channel_id, request_bytes):
        if self._on_active_changed is not None:
            self._on_active_changed(True)   # → AndroidAutoModule.show()

    def on_media_channel_stop(self, channel_id, request_bytes):
        if self._on_active_changed is not None:
            self._on_active_changed(False)  # → AndroidAutoModule.hide()
```

Il callback `on_active_changed` è l'unico punto di contatto tra il layer di protocollo e la UI:
quando il video diventa attivo/inattivo, l'`Orchestrator` notifica l'`AndroidAutoModule` che mostra
o nasconde il widget GStreamer. È iniettato al momento della costruzione nell'`Orchestrator` principale.

---

## 5. `handlers/sensor/` — Sensor Source

### 5.1 `sensor_event_handler.py` — Risposta Differenziata per Tipo

```python
def on_sensor_start_request(self, request_bytes):
    req = SensorRequest()
    req.ParseFromString(request_bytes)

    resp = SensorStartResponseMessage()
    resp.status = STATUS_SUCCESS

    batch = None
    if req.type == SENSOR_DRIVING_STATUS_DATA:
        batch = SensorBatch()
        batch.driving_status_data.add().status = DRIVE_STATUS_UNRESTRICTED
    elif req.type == SENSOR_NIGHT_MODE:
        batch = SensorBatch()
        batch.night_mode_data.add().night_mode = False

    return resp.SerializeToString(), batch.SerializeToString() if batch else b""
```

La `SensorStartResponse` è seguita immediatamente da un `SensorEventIndication` con il valore
iniziale del sensore — il telefono ne ha bisogno per inizializzare la propria UI.
`DRIVE_STATUS_UNRESTRICTED` sblocca tutte le funzionalità che sarebbero limitate in modalità guida
(es. tastiera, contenuti non sicuri alla guida).

### 5.2 Invio Doppio: Response + Indication

```python
def on_sensor_start_request(self, handler, payload):
    res_bytes, batch_bytes = self._orchestrator.on_sensor_start_request(payload)
    if res_bytes:
        channel.send_sensor_start_response(resp, strand, "Sensor/SensorStartResponse")
    if batch_bytes:
        channel.send_sensor_event_indication(batch, strand, "Sensor/SensorEventIndication")
    channel.receive(handler)
```

Unico tra gli handler "semplici" a inviare **due messaggi in sequenza** in risposta a una singola richiesta.

---

## 6. `handlers/input/` — Input Source (Handler più ricco in Python)

Il file `input_event_handler.py` è il **più ricco del progetto lato Python** (~230 righe).

### 6.1 `InputOrchestrator` — Key Binding con Subset Check

```python
class InputOrchestrator:
    def __init__(self, supported_keycodes=None):
        self._supported_keycodes = set(supported_keycodes or [])
        self._bound_keycodes = set()

    def on_key_binding_request(self, request_bytes):
        req = KeyBindingRequest()
        req.ParseFromString(request_bytes)
        if self._supported_keycodes:
            requested = set(req.keycodes)
            if not requested.issubset(self._supported_keycodes):
                status = STATUS_KEYCODE_NOT_BOUND
            else:
                self._bound_keycodes = requested
        resp.status = status
        return resp.SerializeToString(), b""
```

### 6.2 `default_supported_keycodes()` — 19 Keycodes HW

Funzione standalone che costruisce la lista dei keycodes supportati dalla HeadUnit:
HOME, BACK, CALL, ENDCALL, DPAD (5 direzioni), ENTER, MENU, media keys (play/pause/next/prev),
VOLUME (up/down/mute), VOICE_ASSIST. Il `try/except` per singolo keycode garantisce che
keycodes mancanti in versioni diverse di aasdk non blocchino l'intera lista.

### 6.3 Frontend API Touch Completa

```python
class InputEventHandlerLogic:
    def send_touch(self, action, pointers, action_index=0, disp_channel_id=0):
        report = InputReport()
        report.timestamp = int(time.time_ns() // 1000)  # microsecondi
        touch = TouchEvent()
        touch.action = int(action)
        touch.action_index = int(action_index)
        for x, y, pid in pointers:
            p = touch.pointer_data.add()
            p.x = int(x); p.y = int(y); p.pointer_id = int(pid)
        report.touch_event.CopyFrom(touch)
        self._send_input_report_bytes(report.SerializeToString(), "Input/TouchEvent")

    def send_touch_down(self, x, y, pointer_id=0, disp_channel_id=0): ...
    def send_touch_up(self, x, y, pointer_id=0, disp_channel_id=0): ...
    def send_touch_move(self, x, y, pointer_id=0, disp_channel_id=0): ...
    def send_key(self, keycode, down=True, metastate=0, longpress=False): ...
```

Il timestamp in microsecondi (`time.time_ns() // 1000`) è necessario per la sincronizzazione
del touch con il rendering video lato telefono — un timestamp errato causa glitch nell'interfaccia AA.

### 6.4 `InputFrontend` + `build_pyqt6_keycode_map()`

```python
class InputFrontend:
    def send_key_qt(self, qt_key, down=True, metastate=0, longpress=False, disp_channel_id=0):
        keycode = self._keycode_map.get(int(qt_key))
        if keycode is None:
            raise ValueError(f"Unsupported Qt key: {qt_key}")
        self._logic.send_key(keycode, down, metastate, longpress, disp_channel_id)
```

`build_pyqt6_keycode_map()` costruisce a runtime un dizionario `{Qt.Key → AAP KeyCode}` con
mapping per 26 lettere, 10 cifre, navigazione DPAD, tasti media e back/home.
`InputFrontend` è il thin adapter usato da `AndroidAutoModule` per tradurre eventi Qt nativi
in `InputReport` AAP — isolamento totale tra logica UI e protocollo AAP.

---

## 7. `handlers/bluetooth/` — Bluetooth (Risposta Doppia)

### 7.1 `bluetooth_event_handler.py` — PairingResponse + AuthData

```python
def on_bluetooth_pairing_request(self, handler, payload):
    pairing_bytes, auth_bytes = self._orchestrator.on_bluetooth_pairing_request(None, payload)
    if pairing_bytes:
        pairing = core.GetProtobuf("...BluetoothPairingResponse")
        pairing.parse_from_string(pairing_bytes)
        channel.send_bluetooth_pairing_response(pairing, strand, "Bluetooth/PairingResponse")
    if auth_bytes:
        auth = core.GetProtobuf("...BluetoothAuthenticationData")
        auth.parse_from_string(auth_bytes)
        channel.send_bluetooth_authentication_data(auth, strand, "Bluetooth/AuthData")
    channel.receive(handler)
```

Il canale Bluetooth richiede l'invio di **due messaggi in sequenza** in risposta a `PairingRequest`:
prima `BluetoothPairingResponse(already_paired=False)`, poi `BluetoothAuthenticationData(auth_data, method=PIN)`.
Il valore di default `auth_data="123456"` è un placeholder — in produzione deve essere letto
dal campo `bt_address` della configurazione o generato dinamicamente.

---

## 8. `handlers/navigation/` — Navigation Status (Sink con 4 eventi)

### 8.1 `navigation_event_handler.hpp` — 4 Override

```cpp
void onStatusUpdate(NavigationStatus)                     // stato navigazione attiva/inattiva
void onTurnEvent(NavigationNextTurnEvent)                 // prossima svolta (simbolo, street name)
void onDistanceEvent(NavigationNextTurnDistanceEvent)     // distanza alla prossima svolta
void onChannelError(aasdk::error::Error)
```

### 8.2 `navigation_event_handler.py` — Sink Puro

```python
class NavigationOrchestrator:
    def on_status_update(self, request_bytes):  return None
    def on_turn_event(self, request_bytes):     return None
    def on_distance_event(self, request_bytes): return None
```

Navigation è un canale **unidirezionale dal telefono**: la HeadUnit non invia nulla oltre
all'apertura del canale. I dati ricevuti (turni, distanze, stato) sono disponibili tramite
override dell'`Orchestrator` per implementare un HUD di navigazione sovrapposto al video.

---

## 9. `handlers/phone_status/` e `handlers/media_browser/`

Entrambi seguono la struttura minimale: `onChannelOpenRequest` + `onChannelError`, `Orchestrator` stub.

- **PhoneStatus**: riceve notifiche di chiamate in arrivo e stato rete — hook disponibile via override.
- **MediaBrowser**: permette alla HeadUnit di navigare la libreria media del telefono — funzionalità
  avanzata non implementata nella versione base, ma il canale si apre correttamente per non bloccare
  la `ServiceDiscovery` completa della sessione AAP.

---

## 10. `handlers/media_playback_status/` — Metadata e Playback

### 10.1 `media_playback_status_event_handler.py` — Orchestrator con Parse Reale

```python
class MediaPlaybackStatusOrchestrator:
    def on_metadata_update(self, channel_id, request_bytes):
        metadata = MediaPlaybackMetadata()
        metadata.ParseFromString(request_bytes)
        return metadata  # oggetto protobuf, non bytes

    def on_playback_update(self, channel_id, request_bytes):
        playback = MediaPlaybackStatus()
        playback.ParseFromString(request_bytes)
        return playback  # oggetto protobuf, non bytes
```

Diversamente dagli altri Orchestrator che ritornano `bytes`, questo ritorna **oggetti protobuf deserializzati**.
I dati disponibili tramite override includono: titolo brano, artista, album, artwork URL, durata,
posizione di riproduzione, stato play/pause/shuffle/repeat — utili per un HUD media nella UI.

---

## 11. `handlers/media_source/` — Microfono (Thread Dedicato C++)

`MediaSource` è il secondo handler (insieme ad `Audio`) con hot-path diretto a `AvCore`.

### 11.1 `media_source_event_handler.hpp` — `micLoop()` su Thread Dedicato

```cpp
void onMediaSourceOpenRequest(
    const aap_protobuf::service::media::source::message::MicrophoneRequest &request) override
{
    if (av_core_) {
        if (request.open()) {
            av_core_->setMicActive(true);
            av_core_->startMicCapture();
            startMicStreaming();   // lancia mic_thread_
        } else {
            stopMicStreaming();    // join mic_thread_
            av_core_->stopMicCapture();
            av_core_->setMicActive(false);
        }
    }
    CALL_PY(request);
}

void micLoop()
{
    while (mic_running_.load()) {
        nemo::AvFrame frame;
        if (!av_core_->popMicFrame(frame, 50)) continue;  // blocca max 50ms
        sendMicFrame(std::move(frame));
    }
}
```

Il microfono usa un **thread dedicato C++** (`mic_thread_`) separato dalla strand di Boost.Asio.
`popMicFrame` blocca per max 50ms in attesa di un frame PCM catturato dall'`AvCore`.
`sendMicFrame` effettua `strand_.dispatch` prima di inviare — garantisce thread-safety sul canale aasdk.

### 11.2 `std::atomic<bool> mic_running_` — Start/Stop Lock-Free

```cpp
void startMicStreaming() {
    if (mic_running_.exchange(true)) return;  // già in esecuzione, no-op
    mic_thread_ = std::thread([self = shared_from_this()]() { self->micLoop(); });
}

void stopMicStreaming() {
    mic_running_.store(false);
    if (mic_thread_.joinable()) mic_thread_.join();
}
```

`exchange(true)` è l'idioma lock-free per "avvia solo se non già avviato" — immune a doppia
attivazione in caso di race condition. Il `join()` nel distruttore garantisce che il thread
non sopravviva all'handler (no dangling thread dopo disconnect USB).

### 11.3 `channel_ptr` / `strand_ptr` nel Binding

```cpp
.def_property_readonly("channel_ptr",
    [](app::MediaSourceEventHandler &self) {
        return reinterpret_cast<std::uintptr_t>(self.channel().get());
    })
.def_property_readonly("strand_ptr",
    [](app::MediaSourceEventHandler &self) {
        return reinterpret_cast<std::uintptr_t>(&self.strand());
    })
```

Questi due attributi espongono i raw pointer come interi a Python — utili per debugging
o per passare il contesto a un futuro binding GStreamer che opera fuori dal GIL.
`MediaSourceChannel` istanzia concretamente `MicrophoneAudioChannel` (non la base `MediaSourceService`).

---

## 12. `handlers/radio/`, `handlers/vendor_extension/`, `handlers/generic_notification/`

Tutti e tre seguono il pattern minimale identico:

```
hpp: onChannelOpenRequest + onChannelError → CALL_PY
py:  XxxOrchestrator (STATUS_SUCCESS) + XxxEventHandlerLogic (open + error)
```

- **Radio**: canale DAB/FM — non implementato, si apre per non bloccare session discovery.
- **VendorExtension**: canale extension OEM-specific — stub aperto.
- **GenericNotification**: notifiche generiche dal telefono — stub aperto.

La strategia "apri e tieni vivo" è deliberata: un canale che non risponde alla `ChannelOpenRequest`
durante la `ServiceDiscovery` blocca l'intera sessione AAP — meglio aprirlo come stub
che lasciarlo pendente.

---

## 13. `handlers/wifi_projection/` — WiFi Credentials (Non-Stub)

WifiProjection è l'unico tra i canali "secondari" a implementare logica di risposta reale.

### 13.1 `wifi_projection_event_handler.py` — Credenziali Configurabili

```python
class WifiProjectionOrchestrator:
    def __init__(self, ssid: str = "NemoHeadUnit", password: str = "12345678",
                 security_mode: int | None = None):
        self._ssid = ssid
        self._password = password
        self._security_mode = security_mode or WifiSecurityMode.WPA2_PERSONAL

    def on_wifi_credentials_request(self, channel_id, request_bytes):
        resp = WifiCredentialsResponse()
        resp.access_point_type = AccessPointType.STATIC
        resp.car_wifi_ssid = self._ssid
        resp.car_wifi_password = self._password
        resp.car_wifi_security_mode = self._security_mode
        return resp.SerializeToString()
```

La risposta `WifiCredentialsResponse` fornisce al telefono le credenziali dell'AP WiFi della HeadUnit
per passare da USB a WiFi projection. I valori di default (`NemoHeadUnit` / `12345678`) sono placeholder —
in produzione vanno letti dalla config. `AccessPointType.STATIC` indica un AP con IP fisso.

---

## 14. Considerazioni Architetturali e Punti di Attenzione

### 14.1 GIL Safety — Regola Fondamentale Rispettata

I tre handler con hot-path C++ (`audio`, `video`, `media_source`) rispettano la regola fondamentale:
- Buffer PCM audio: C++ → `AvCore::pushAudio()` senza mai acquisire il GIL
- Frame H.264 video: C++ → GStreamer via window ID nativo senza mai acquisire il GIL
- Frame PCM mic: thread C++ → `av_core_->popMicFrame()` → `strand.dispatch` senza GIL

Python riceve solo **notifiche eventi** (stringhe vuote o payload protobuf di controllo),
mai buffer media in tempo reale.

### 14.2 `pybind11::keep_alive` — Lifecycle Management Corretto

Ogni binding handler usa:
```cpp
pybind11::keep_alive<1, 2>()  // handler mantiene vivo strand
pybind11::keep_alive<1, 3>()  // handler mantiene vivo channel
```
Senza questi marker, Boost.Asio potrebbe deallocare strand/channel mentre i loro raw reference
sono ancora in uso da callback C++ in-flight — segfault garantito.

### 14.3 `pybind11::module_local()` su Tutti i Canali

Tutti i canali usano `pybind11::module_local()` per evitare conflitti di registrazione del tipo
quando il modulo viene importato più volte (es. in test o in futuro split del modulo pybind11).

### 14.4 Typo Ricorrente — Candidato a Fix `chore/`

In 11 file `.py` è presente un typo nel messaggio `RuntimeError`:
```python
raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
#                                                                ↑ manca spazio
```
Presente in: `video`, `sensor`, `input`, `media_playback_status`, `media_source`, `navigation`,
`phone_status`, `media_browser`, `radio`, `vendor_extension`, `generic_notification`.
Non impatta il funzionamento ma è candidato a un `chore/fix-typo-error-messages`.

---

## 15. Matrice Completa degli Handler

| Handler | C++ Events (hpp) | Python Orchestrator | Frontend API | AvCore | Thread |
|---|---|---|---|---|---|
| `control` | 13 (version, handshake, discovery, focus, ping, bye...) | via control_modules/ | — | ✗ | strand |
| `audio` | 6 + `pushAudio` diretto | `AudioOrchestrator` (session_id, ack) | — | ✓ (sink) | strand |
| `video` | 7 + VideoFocus post-setup | `VideoOrchestrator` (on_active_changed) | — | ✓ (sink) | strand |
| `media_source` | 5 + `startMicCapture` + `micLoop` | `MediaSourceOrchestrator` (session_id) | — | ✓ (source) | mic_thread_ |
| `sensor` | 3 | `SensorOrchestrator` (DrivingStatus, NightMode) | — | ✗ | strand |
| `input` | 3 | `InputOrchestrator` (keybinding) | `InputFrontend` + keycode map Qt6 | ✗ | strand |
| `bluetooth` | 4 | `BluetoothOrchestrator` (pairing + auth) | — | ✗ | strand |
| `wifi_projection` | 3 + WifiCredentials | `WifiProjectionOrchestrator` (SSID/pwd) | — | ✗ | strand |
| `navigation` | 5 (sink) | `NavigationOrchestrator` (hook) | — | ✗ | strand |
| `media_playback_status` | 4 + metadata/playback parse | `MediaPlaybackStatusOrchestrator` (hook) | — | ✗ | strand |
| `phone_status` | 2 (stub) | `PhoneStatusOrchestrator` (hook) | — | ✗ | strand |
| `media_browser` | 2 (stub) | `MediaBrowserOrchestrator` (hook) | — | ✗ | strand |
| `radio` | 2 (stub) | `RadioOrchestrator` (hook) | — | ✗ | strand |
| `vendor_extension` | 2 (stub) | `VendorExtensionOrchestrator` (hook) | — | ✗ | strand |
| `generic_notification` | 2 (stub) | `GenericNotificationOrchestrator` (hook) | — | ✗ | strand |

---

## 16. Copertura Finale — 67/67

| Chunk | File coperti | Contenuto |
|---|---|---|
| 1 | CMakeLists, app core, config, channel_manager, transport_stack, usb_hub_manager, main, orchestrator, av_core.hpp, io_context_runner, control_event_handler.py | Build system + Core infrastruttura |
| 2 | android_auto.py, event_binding.hpp, protobuf_message.hpp, service_discovery.py, UI modules (base, header, footer, keyboard, settings, disconnected, main_window) | UI + Binding comuni + Service Discovery |
| 3 | external_bindings (usb_context, aasdk_usb, transport, cryptor, messenger, logger_binding) | Stack USB/TLS |
| 4 | nemo_core.py, py_logging, logging.hpp, channels.hpp, event_handler_utils.hpp, control_modules (proto, handshake, focus, utils), config_module.py, generate_protos.sh | Core orchestration + Control submodules |
| **5** | **Tutti i 29 handler rimanenti (15 coppie hpp+py + control.hpp)** | **Handler layer completo** |
| **TOTALE** | **67/67** | **100% del codebase** |
