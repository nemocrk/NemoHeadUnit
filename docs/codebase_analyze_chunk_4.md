# NemoHeadUnit — Analisi Codebase · Chunk 4/4

> **Generato automaticamente** · 2026-03-17  
> Copertura: `app/nemo_core.py`, `app/core/py_logging.py`, `app/core/logging.hpp`, `app/core/transport_stack.py`, `app/core/usb_hub_manager.py`, `external_bindings/channels.hpp`, `handlers/common/event_handler_utils.hpp`, `handlers/control/control_modules/proto.py`, `handlers/control/control_modules/handshake.py`, `handlers/control/control_modules/focus.py`, `handlers/control/control_modules/utils.py`, `app/ui/modules/config_module.py`, `scripts/generate_protos.sh`

---

## 1. `app/nemo_core.py` — Bootstrap dei Binding C++

`nemo_core.py` è il **punto di ingresso centralizzato per i binding nativi**: configura `sys.path` per trovare la directory `build/` (moduli `.so` compilati) e `aasdk_proto/` (moduli protobuf generati), poi importa tutti i moduli C++ in un unico namespace.

### 1.1 Moduli importati

```python
io_context    = __import__("io_context")
aasdk_logging = __import__("aasdk_logging")
cryptor       = __import__("cryptor")
aasdk_usb     = __import__("aasdk_usb")
transport     = __import__("transport")
messenger     = __import__("messenger")
channels      = __import__("channels")
event         = __import__("event")
```

`__import__` con stringa letterale (invece di `import`) permette di importare moduli il cui nome è risolto a runtime e non riconoscibile staticamente da linter/type checkers — pattern necessario per i `.so` pybind11 il cui nome dipende dall'architettura target.

### 1.2 `init_logging()` — API multipla resiliente

```python
def init_logging():
    # Prima: configura Python logging
    py_logging.configure_from_env()

    if _is_truthy(os.getenv(_LOG_ENV_AASDK, "0")):
        # Tenta le tre possibili versioni dell'API aasdk_logging
        if hasattr(aasdk_logging, "set_aasdk_log_level"):
            aasdk_logging.set_aasdk_log_level(...)
        elif hasattr(aasdk_logging, "configure_aasdk_logging"):
            aasdk_logging.configure_aasdk_logging(...)
        elif hasattr(aasdk_logging, "enable_aasdk_logging"):
            aasdk_logging.enable_aasdk_logging()
```

La strategia `hasattr`-cascade garantisce retrocompatibilità con versioni precedenti dei binding senza if/else a compile-time. Se `CORE_LOG` o `AASDK_CORE_LOG` sono truthy all'import, `init_logging()` viene chiamato automaticamente — zero configurazione esplicita richiesta per sessioni di debug.

---

## 2. `app/core/py_logging.py` — Python Logger con Livello TRACE

Estende il logging standard Python aggiungendo un livello **TRACE (valore 5)**, inferiore a DEBUG (10). La configurazione è guidata dalla variabile d'ambiente `CORE_LOG`.

### 2.1 Difesa contro import ciclici e shadowing

```python
if hasattr(_maybe_logging, "basicConfig"):
    py_logging = _maybe_logging
else:
    # import diretto dal filesystem stdlib (evita shadowing da modulo locale)
    _spec = importlib.util.spec_from_file_location("_stdlib_logging", _logging_path)
    _module = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_module)
    py_logging = _module
```

Il fallback via `spec_from_file_location` risolve edge case dove un modulo locale `logging.py` oscura quello stdlib — problema comune in progetti embedded dove `sys.path` è non standard.

### 2.2 Livelli supportati

| Stringa `CORE_LOG` | Livello |
|---|---|
| `trace`, `verbose`, `2` | TRACE (5) |
| `debug`, `1`, `true`, `yes`, `on` | DEBUG (10) |
| `info` | INFO (20) |
| `warn`, `warning` | WARNING (30) |
| `error` | ERROR (40) |
| (vuoto / non riconosciuto) | INFO (default) |

### 2.3 `_LoggerAdapter` e singleton `_def_loggers`

`_LoggerAdapter` wrappa `logging.Logger` aggiungendo `.trace()`. Il dizionario `_def_loggers` è un singleton per nome: tutti i moduli che chiamano `get_logger("app.usb")` ottengono la **stessa istanza adapter**, senza duplicare handler o formatter.

### 2.4 Configurazione lazy (`_ensure_configured`)

La chiamata a `basicConfig()` avviene solo al primo `get_logger()` — non all'import. Questo permette al modulo di essere importato in unit test senza side effect sul logging di sistema.

---

## 3. `app/core/logging.hpp` — AppLogger C++ (header-only)

`AppLogger` è il **logger C++ interno** dell'applicazione, usato da `AvCore` e dagli handler nativi. È un singleton thread-safe (via `static` locale, C++11 guarantee).

### 3.1 Caratteristiche

| Feature | Dettaglio |
|---|---|
| Livelli | `TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4` |
| Config | Variabile d'ambiente `CORE_LOG` (stessa di Python) |
| Output | `stderr` per ERROR, `stdout` per tutti gli altri |
| Timestamp | `YYYY-MM-DD HH:MM:SS,mmm` via `localtime_r` + `chrono` |
| Forward a aasdk | Opzionale via `NEMO_AVCORE_LOG_TO_AASDK=1` |
| Categoria aasdk | `LogCategory::AUDIO` (fisso, usato da AvCore) |

### 3.2 Pattern `LogStream` (RAII)

```cpp
APP_LOG_INFO("av_core") << "Pipeline avviata frame=" << frame_count;
// equivalente a:
{
    LogStream _s(AppLogLevel::INFO, "av_core", __FUNCTION__, __FILE__, __LINE__);
    _s << "Pipeline avviata frame=" << frame_count;
}  // <-- ~LogStream() invia il messaggio
```

Il macro crea un `LogStream` temporaneo RAII: il distruttore (chiamato a fine espressione) invia il messaggio accumulato in `ostringstream` ad `AppLogger::log()`. **Zero buffer condivisi, zero lock**, ogni messaggio è costruito localmente sullo stack.

### 3.3 Coerenza con Python

`AppLogger::parseLevel()` e `_parse_level()` in `py_logging.py` accettano le **stesse stringhe** (`trace`, `debug`, `info`, `warn`, `error`, `1`, `true`) — la stessa variabile `CORE_LOG` controlla entrambi i layer.

---

## 4. `app/core/transport_stack.py` — TransportStack

`TransportStack` è la **implementazione Python della catena TLS/USB**. Assembla in sequenza tutti i binding C++ descritti in `external_bindings/`.

### 4.1 Catena di costruzione

```
USBTransport(io_ctx_ptr, aoap_device)
    → SSLWrapper()
        → Cryptor(ssl_wrapper)
            → cryptor.init()           ← setup SSL context
                → MessageInStream(io_ctx_ptr, transport, cryptor)
                → MessageOutStream(io_ctx_ptr, transport, cryptor)
                    → Messenger(io_ctx_ptr, in_stream, out_stream)
```

### 4.2 `stop()` difensivo

```python
def stop(self):
    if hasattr(self, "messenger") and self.messenger:
        self.messenger.stop()
    if hasattr(self, "transport") and self.transport:
        self.transport.stop()
    if hasattr(self, "cryptor") and self.cryptor:
        self.cryptor.deinit()
```

Il check `hasattr` protegge da chiamate `stop()` su oggetti parzialmente costruiti: se il costruttore lancia eccezione a metà (es. `USBTransport` fallisce), i campi successivi non esistono e `stop()` non crasha.

### 4.3 Ordine di stop

L'ordine `messenger → transport → cryptor.deinit()` è deliberato:
1. `messenger.stop()` ferma la lettura/scrittura messaggi AAP in corso
2. `transport.stop()` chiude la connessione USB
3. `cryptor.deinit()` libera il contesto SSL (dopo che il canale è già chiuso)

Invertire l'ordine potrebbe causare write su transport già chiuso durante il TLS close_notify.

---

## 5. `app/core/usb_hub_manager.py` — UsbHubManager

`UsbHubManager` gestisce l'intero ciclo di vita USB: dalla creazione del contesto libusb fino al reset hardware del dispositivo.

### 5.1 Flessibilità costruttore

```python
def __init__(self, io_context_ptr, libusb_context=None, libusb_context_ptr=None):
```

| Modalità | Quando usarla |
|---|---|
| Nessun param libusb | Produzione: crea e inizializza `LibusbContext` internamente |
| `libusb_context` esistente | Test/mock: usa contesto passato dall'esterno |
| Solo `libusb_context_ptr` | Interop avanzata con puntatore raw (warning logato) |

### 5.2 `hard_teardown()` — Reset Aggressivo

```python
def hard_teardown(self):
    self.stop()                        # USBHub.cancel()
    self._libusb_context.stop()        # libusb_exit() + timer cancel
    time.sleep(0.2)                    # grace period per il sistema operativo
    self._try_usbreset()               # reset hardware del dispositivo
    self._usb_hub = None               # drop tutti i riferimenti C++
    self._usb_wrapper = None
    self._query_factory = None
    self._query_chain_factory = None
    self._libusb_context = None
    self._libusb_context_ptr = None
```

Il `None`-ing esplicito di tutti i campi forza il GC Python a rilasciare gli `shared_ptr` C++ immediatamente, senza attendere che il `UsbHubManager` stesso venga raccolto. Questo è essenziale prima di ricreare la chain USB nel restart automatico dell'`Orchestrator`.

### 5.3 `_try_usbreset()` — Strategia a 3 livelli

```
[1] usbreset 18d1:2d00   (forma canonica AOAP)
        ↓ se rc!=0
[2] lsusb → bus/dev      (re-risolve path ad ogni tentativo)
        ↓ 3 tentativi con sleep 0.2s
[3] silent skip          (se usbreset non è sul PATH)
```

Il re-risolve del path via `lsusb` ad ogni tentativo è necessario perché il numero device USB può cambiare durante il reset (es. `/dev/bus/usb/001/004` → `/dev/bus/usb/001/007`). Cercare il device una sola volta e riusare il path fallirebbe al secondo tentativo.

### 5.4 `create_aoap_device()`

```python
def create_aoap_device(self, device_handle):
    return _usb.AOAPDevice.create(self._usb_wrapper, self._io_context_ptr, device_handle)
```

Separa la discovery (USBHub) dalla creazione del device AOAP — l'`Orchestrator` chiama prima `start(on_device, on_err)`, poi nella callback `on_device(handle)` chiama `create_aoap_device(handle)`. Architettura a due fasi che permette di intercettare il `DeviceHandle` prima di creare il trasporto.

---

## 6. `external_bindings/channels.hpp` — Canali AAP

Espone a Python l'enum `ChannelId` e le classi dei canali aasdk.

### 6.1 `ChannelId` enum (20 valori)

```python
import channels
cid = channels.ChannelId.MEDIA_SINK_VIDEO
cid = channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO
cid = channels.ChannelId.INPUT_SOURCE
# ...20 valori totali
```

Usato da `ChannelManager` per costruire ogni channel con il `ChannelId` corretto e da `ServiceDiscoveryResponse` per dichiarare i canali supportati.

### 6.2 Classi canale esposte

| Classe Python | Canale | Direzione |
|---|---|---|
| `VideoMediaSinkService` | Video H.264 | Telefono → HeadUnit |
| `SensorSourceService` | GPS, giroscopio, ecc. | HeadUnit → Telefono |
| `InputSourceService` | Touch/key events | HeadUnit → Telefono |
| `NavigationStatusService` | Status navigazione | HeadUnit → Telefono |

**Nota**: I canali audio (`AudioMediaSinkService` per media/guidance/system, `MediaSourceMicrophoneService` per il microfono) non sono esposti qui — vengono costruiti internamente dagli handler C++ tramite il `ChannelId`.

### 6.3 `StrandPtr` deref pattern

```cpp
.def(py::init([](StrandPtr strand, Messenger::Pointer messenger, ChannelId channel_id) {
    return std::make_shared<VideoMediaSinkService>(
        *strand,              // <-- deref: aasdk vuole strand&, non shared_ptr
        std::move(messenger),
        channel_id);
}))
```

aasdk accetta `strand&` nei costruttori dei canali, ma Python non può passare riferimenti raw. Il lambda dereferenzia lo `shared_ptr<strand>` con `*strand` — il `shared_ptr` mantiene il lifetime del `strand` finché il canale è vivo.

---

## 7. `handlers/common/event_handler_utils.hpp` — Utilities Comuni

Header condiviso dai 15+ handler C++.

### 7.1 Macro `CALL_PY`

```cpp
#define CALL_PY(msg) (binding_)->call(this, __func__, msg)
```

Ogni metodo C++ dell'handler (es. `onVideoMediaWithTimestamp`) si riduce a una singola chiamata `CALL_PY(message)`. `__func__` porta il nome del metodo C++ all'`EventBinding`, che lo converte con `camel_to_snake` per trovare il metodo Python corrispondente. Zero mapping esplicito C++ → Python.

### 7.2 `EventHandlerBase<ChannelPtr>`

```cpp
template <typename ChannelPtr>
class EventHandlerBase {
protected:
    boost::asio::io_service::strand &strand_;
    ChannelPtr channel_;
};
```

Template base CRTP-like: tutti gli handler ereditano `strand_` e `channel_` senza ridefinirli. Riduce il boilerplate da ~10 righe a 1 riga di ereditarietà per ogni handler.

### 7.3 `with_promise()` — due overload

**Overload Python** (con callback, GIL managed):
```cpp
with_promise(strand_, [&](auto p) {
    channel_->sendAck(std::move(p));
}, py_on_complete);   // <-- py::object callback
```

**Overload C++ puro** (zero overhead GIL):
```cpp
with_promise(strand_, [&](auto p) {
    channel_->sendAck(std::move(p));
});
```

Il deleter custom del callback Python (`py::gil_scoped_acquire` nel distruttore) segue lo stesso pattern di `make_gil_safe_function` in `aasdk_usb.hpp` — coerenza assoluta nella gestione del GIL cross-layer in tutto il codebase.

---

## 8. `handlers/control/control_modules/proto.py` — Registry Protobuf

Registry centralizzato di **tutti i moduli protobuf** dell'applicazione.

### 8.1 Pattern `_try_import`

```python
def _try_import(fn):
    global PROTOBUF_ERROR
    try:
        fn()
    except ImportError as e:
        if PROTOBUF_ERROR is None:
            PROTOBUF_ERROR = e   # registra solo il primo errore

_try_import(lambda: _set("AuthResponse_pb2", "aasdk_proto..."))
# ...35+ moduli

PROTOBUF_AVAILABLE = PROTOBUF_ERROR is None
```

Se un singolo modulo non è disponibile (es. `generate_protos.sh` non eseguito), l'app si avvia comunque. `PROTOBUF_AVAILABLE` è il flag globale verificato da `HandshakeState.__init__()` e da `ConfigModule` prima di usare le chiavi enum proto.

### 8.2 Moduli importati per categoria

| Categoria | Moduli (_pb2) |
|---|---|
| Control | `AuthResponse`, `ServiceDiscoveryResponse`, `DriverPosition`, `PingResponse`, `AudioFocusNotification`, `AudioFocusRequest`, `AudioFocusRequestType`, `AudioFocusStateType`, `NavFocusNotification`, `NavFocusType`, `MessageStatus` |
| Media | `MediaSinkService`, `VideoCodecResolutionType`, `VideoFrameRateType`, `AudioStreamType`, `MediaCodecType`, `AudioConfiguration` |
| Services | `SensorSourceService`, `SensorType`, `InputSourceService`, `TouchScreenType`, `NavigationStatusService`, `BluetoothService`, `BluetoothPairingMethod`, `GenericNotificationService`, `MediaBrowserService`, `MediaPlaybackStatusService`, `PhoneStatusService`, `RadioService`, `VendorExtensionService`, `WifiProjectionService` |

### 8.3 Doppio import path

`_set(name, module_path)` usa `__import__(module_path, fromlist=["*"])` mentre `_set_symbol(name, module_path, symbol)` estrae solo un simbolo. La seconda forma è usata per enum che risiedono in moduli con namespace diverso dal nome del file.

---

## 9. `handlers/control/control_modules/handshake.py` — HandshakeState

`HandshakeState` è la **TLS state machine** del canale di controllo AAP.

### 9.1 Flusso TLS completo

```
[Telefono]
    ↓ VersionRequest
[HeadUnit] on_version_response(major, minor, status)
    → _step_handshake_and_collect()  ← genera Client Hello
    → ritorna bytes TLS al canale (inviati al telefono)

[Telefono]
    ↓ Server Hello + Certificate + ServerHelloDone
[HeadUnit] on_handshake(payload)
    → write_handshake_buffer(payload)  ← inietta nel Cryptor
    → _step_handshake_and_collect()    ← genera Client Key Exchange + Finished
    → se done=True: handshake_done = True

[HeadUnit] get_auth_complete_response()
    → AuthResponse(status=STATUS_SUCCESS)
```

### 9.2 `_drain_tls_out()` — drain loop con safety guard

```python
def _drain_tls_out(self, max_iters=32) -> bytes:
    out = b""
    for _ in range(max_iters):
        chunk = self.cryptor.read_handshake_buffer()
        if not chunk:
            break
        out += chunk
    return out
```

TLS può generare **più chunk** per un singolo flight (es. Certificate + CertificateVerify + ChangeCipherSpec + Finished). Il loop con `max_iters=32` è un safety guard contro loop infiniti in caso di bug del Cryptor. Senza il loop, si invierebbe solo il primo chunk e l'handshake si congelerebbe silenziosamente.

### 9.3 `_step_handshake_and_collect()`

```python
def _step_handshake_and_collect(self, max_steps=32):
    done = False
    out = b""
    for _ in range(max_steps):
        done = bool(self.cryptor.do_handshake())
        drained = self._drain_tls_out()
        if drained:
            out += drained
            continue   # potrebbe esserci altro da drenare
        break
    return done, out
```

Il `continue` nella logica di drain permette di raccogliere tutti i chunk generati da più passi `do_handshake()` consecutivi in una singola chiamata — ottimizzazione che riduce il numero di messaggi inviati al telefono.

---

## 10. `handlers/control/control_modules/focus.py` — Gestori Focus

Contiene i 5 handler "semplici" del canale di controllo — quelli che non richiedono stato.

| Handler | Input | Output | Note |
|---|---|---|---|
| `on_ping_request` | PingRequest | PingResponse(timestamp=now_ms) | Timestamp in millisecondi |
| `on_audio_focus_request` | AudioFocusRequest | AudioFocusNotification(GAIN/LOSS) | RELEASE → LOSS, altrimenti GAIN |
| `on_navigation_focus_request` | NavFocusRequest | NavFocusNotification(PROJECTED) | Sempre PROJECTED |
| `on_voice_session_request` | VoiceSessionRequest | `b""` | Sink silente |
| `on_battery_status_notification` | BatteryStatus | `b""` | Sink silente |

**Logica Audio Focus**: la HeadUnit concede **sempre** il focus audio (`AUDIO_FOCUS_STATE_GAIN`), eccetto quando il telefono rilascia esplicitamente (`AUDIO_FOCUS_RELEASE`). Non implementa focus stealing tra app — design deliberato per una HeadUnit dedicata.

**Logica Nav Focus**: risponde sempre `NAV_FOCUS_PROJECTED` (navigazione proiettata sulla HeadUnit) — il telefono usa questo valore per sapere di non mostrare la propria UI di navigazione.

---

## 11. `handlers/control/control_modules/utils.py` — `log_and_send`

```python
def log_and_send(label: str, data: bytes) -> bytes:
    if _logger.is_enabled_for(TRACE_LEVEL):
        preview = binascii.hexlify(data[:32]).decode()
        _logger.trace("Action=%s size=%d bytes hex=%s", label, len(data), preview)
    return data
```

Utility trasparente per il debug del protocollo di controllo. Il check `is_enabled_for(TRACE_LEVEL)` **prima** di costruire la stringa hex previene la serializzazione dei dati binari quando TRACE non è attivo — ottimizzazione hot-path, dato che ogni messaggio di controllo passa per questa funzione.

---

## 12. `app/ui/modules/config_module.py` — ConfigModule

`ConfigModule` è il pannello di configurazione completo dell'app, con UI a schede scrollabili ottimizzata per touchscreen embedded.

### 12.1 Struttura a 5 tab

```
ConfigModule (UIModule, region="page")
└─ QTabWidget
   ├─ General   ← instance_name, mode (USB/WiFi), bt_address
   ├─ Audio     ← Audio Output + 4 stream (Media/Guidance/System/Mic)
   ├─ Video     ← codec, resolution, frame_rate, density, margins
   ├─ Services  ← checkbox per 16 canali AASDK abilitabili
   └─ Logging   ← core_level + aasdk_level (da LogLevel enum)
```

### 12.2 `NumberControl` — Widget Slider+Stepper

```python
class NumberControl(QWidget):
    # Layout: [minus:44px] [====slider====] [plus:44px] [value_label]
```

Widget riutilizzabile che combina `QSlider` + pulsanti `-`/`+` per input numerico touch-friendly. I pulsanti hanno `setFixedSize(44, 44)` — dimensione minima per target touch su schermi embedded (WCAG 2.5.5 suggerisce 44×44px). `_bump(±step)` con clamp `[min, max]` previene valori fuori range da slider.

### 12.3 Kinetic Scroll via `QScroller`

```python
def _wrap_scroll(self, content: QWidget) -> QScrollArea:
    scroll = QScrollArea()
    scroll.setHorizontalScrollBarPolicy(ScrollBarAlwaysOff)
    QScroller.grabGesture(scroll.viewport(), LeftMouseButtonGesture)
    QScroller.grabGesture(scroll.viewport(), TouchGesture)
    QScroller.grabGesture(content, TouchGesture)
    return scroll
```

Abilita il **kinetic scroll touch** (inerzia dopo il swipe) su ogni tab — essenziale su touchscreen embedded dove il mouse wheel non esiste. `grabGesture` è applicato sia al viewport che al content widget per catturare il gesto in tutti i casi di propagazione eventi Qt.

### 12.4 Integrazione live con protobuf enum

I `QComboBox` per codec video/audio e risoluzioni leggono **direttamente le chiavi degli enum protobuf**:

```python
video_keys = [k for k in proto.VideoCodecResolutionType_pb2
              .VideoCodecResolutionType.keys() if k.startswith("VIDEO_")]
codec_keys = [k for k in proto.MediaCodecType_pb2
              .MediaCodecType.keys() if k.startswith("MEDIA_CODEC_VIDEO_")]
```

I combo riflettono automaticamente tutti i valori AAP senza mapping manuale — se aasdk aggiunge nuovi codec, i combo si aggiornano senza modifiche al codice UI.

### 12.5 `save()` / `reload()` — in-place update

`save()` legge tutti i widget e aggiorna `get_config()` in-place, poi chiama `save_config()`. `reload()` fa il percorso inverso: aggiorna i widget dai valori config **senza ricostruire l'UI** — evita il flash visivo che si avrebbe ricostruendo l'albero Qt.

`save()` per gli stream audio usa un `try/except` per ogni canale — un canale con dati non validi non blocca il salvataggio degli altri.

### 12.6 Tab Services — Canali Abilitabili

```python
KNOWN_CHANNELS = {
    1: "Sensor", 3: "Video", 4: "Media Audio", 5: "Guidance Audio",
    6: "System Audio", 8: "Input", 9: "Microphone", 10: "Bluetooth",
    11: "Phone Status", 12: "Navigation", 13: "Media Playback",
    14: "Media Browser", 15: "Vendor Extension", 16: "Notification",
    17: "WiFi Projection", 18: "Radio"
}  # 16 canali AAP standard
```

Ogni canale è abilitabile/disabilitabile singolarmente via checkbox. Il salvataggio aggiorna `cfg.service_discovery.enabled_channels` che è poi letto da `build_service_discovery_response()` nel canale di controllo.

---

## 13. `scripts/generate_protos.sh` — Generazione Protobuf

Script bash che genera i moduli Python `_pb2.py` dai file `.proto` di aasdk.

### 13.1 Prerequisiti

- Deve essere eseguito **dalla root del progetto** (check `CMakeLists.txt`)
- `cmake -B build` deve essere eseguito prima (per scaricare aasdk via FetchContent)
- `protoc` deve essere disponibile sul `$PATH`

### 13.2 Flusso

```bash
build/_deps/aasdk-src/aasdk_proto/   ← proto "service discovery" aasdk
build/_deps/aasdk-src/protobuf/      ← proto "aap_protobuf" AAP nativo
    ↓
protoc --python_out=./aasdk_proto/
    ↓
sed: fix import Python 3 (2 passaggi)
    ↓
find: crea __init__.py in ogni subdirectory
```

### 13.3 Fix import Python 3

```bash
# Fix 1: path assoluti per cross-package imports
sed -E 's/from aap_protobuf/from aasdk_proto.aap_protobuf/g'

# Fix 2: import relativi per sibling _pb2 nella stessa directory
sed -E 's/^import (.*_pb2)/from . import \1/g'
```

`protoc` genera import `from aap_protobuf.xxx` relativi alla root dei proto. In Python 3, questi import falliscono se `aap_protobuf` non è nel `sys.path`. Il fix `sed` trasforma tutti gli import in stile **package-aware Python 3** in un singolo passaggio su ogni file `.py` generato.

### 13.4 Workaround file corrotto upstream

```bash
find "$AASDK_SRC_DIR" -name "WifiSecurityRequestMessage.proto" -delete
```

`WifiSecurityRequestMessage.proto` è un file corrotto nel repository upstream di aasdk che causa errori `protoc` durante la compilazione. Viene rimosso **prima** della generazione — workaround documentato nello script stesso con commento esplicito.

### 13.5 Cross-platform `sed`

```bash
if [ "$(uname)" == "Darwin" ]; then
    sed -i ''  ...   # macOS: -i richiede argomento stringa (anche se vuoto)
else
    sed -i ...       # Linux: -i senza argomento
fi
```

La differenza di sintassi `sed -i` tra macOS (BSD sed) e Linux (GNU sed) è uno dei più comuni problemi di portabilità bash cross-platform. Il branch `uname` garantisce che lo script funzioni in entrambi gli ambienti di sviluppo.

---

## 14. Copertura Completa — Indice dei 4 Chunk

| File | Chunk |
|---|---|
| `CMakeLists.txt` | 1 |
| `app/main.py` | 1 |
| `app/orchestrator.py` | 1 |
| `app/config.py` | 1 |
| `app/nemo_core.py` | **4** |
| `app/core/channel_manager.py` | 1 |
| `app/core/py_logging.py` | **4** |
| `app/core/logging.hpp` | **4** |
| `app/core/transport_stack.py` | **4** |
| `app/core/usb_hub_manager.py` | **4** |
| `app/media/av_core.hpp` | 1 |
| `handlers/common/event_binding.hpp` | 2 |
| `handlers/common/protobuf_message.hpp` | 2 |
| `handlers/common/event_handler_utils.hpp` | **4** |
| `handlers/control/control_event_handler.py` | 1 |
| `handlers/control/control_modules/service_discovery.py` | 2 |
| `handlers/control/control_modules/handshake.py` | **4** |
| `handlers/control/control_modules/focus.py` | **4** |
| `handlers/control/control_modules/proto.py` | **4** |
| `handlers/control/control_modules/utils.py` | **4** |
| tutti i 13 handler `.py` e `.hpp` | 2 |
| `external_bindings/usb_context.hpp` | 3 |
| `external_bindings/aasdk_usb.hpp` | 3 |
| `external_bindings/transport.hpp` | 3 |
| `external_bindings/cryptor.hpp` | 3 |
| `external_bindings/messenger.hpp` | 3 |
| `external_bindings/channels.hpp` | **4** |
| `external_bindings/io_context_runner.hpp` | 1 |
| `external_bindings/logger_binding.cpp` | 3 |
| `app/ui/main_window.py` | 3 |
| `app/ui/modules/base.py` | 3 |
| `app/ui/modules/header.py` | 3 |
| `app/ui/modules/footer.py` | 3 |
| `app/ui/modules/disconnected.py` | 3 |
| `app/ui/modules/keyboard.py` | 3 |
| `app/ui/modules/settings.py` | 3 |
| `app/ui/modules/android_auto.py` | 2 |
| `app/ui/modules/config_module.py` | **4** |
| `scripts/generate_protos.sh` | **4** |

*Fine analisi — `codebase_analyze_chunk_1.md` + `codebase_analyze_chunk_2.md` + `codebase_analyze_chunk_3.md` + `codebase_analyze_chunk_4.md` coprono il 100% del codebase NemoHeadUnit.*
