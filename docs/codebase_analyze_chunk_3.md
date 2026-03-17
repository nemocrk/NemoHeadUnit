# NemoHeadUnit — Analisi Codebase · Chunk 3/3

> **Generato automaticamente** · 2026-03-17  
> Copertura: `external_bindings/usb_context.hpp`, `external_bindings/aasdk_usb.hpp`, `external_bindings/transport.hpp`, `external_bindings/cryptor.hpp`, `external_bindings/messenger.hpp`, `external_bindings/logger_binding.cpp`, `app/ui/main_window.py`, `app/ui/modules/base.py`, `app/ui/modules/header.py`, `app/ui/modules/footer.py`, `app/ui/modules/disconnected.py`, `app/ui/modules/settings.py`, `app/ui/modules/keyboard.py`, `app/ui/modules/android_auto.py` (complemento), Pattern Globali

---

## 1. `external_bindings/usb_context.hpp` — LibusbContext

`LibusbContext` è il **driver di polling USB** integrato nel loop Boost.Asio. Evita thread separati per il polling libusb: usa un `boost::asio::steady_timer` che si riarma ricorsivamente ogni 10ms (configurabile), chiamando `libusb_handle_events_timeout(ctx, {0,0})` — timeout zero = non bloccante.

### 1.1 Pattern do_poll ricorsivo

```cpp
void do_poll() {
    auto self = shared_from_this();   // strong ref: evita use-after-free
    timer_.expires_after(poll_interval_);
    timer_.async_wait([self](const boost::system::error_code& error) {
        if (error || !self->ctx_) return;   // stop se cancelled o ctx distrutto
        struct timeval tv = {0, 0};
        libusb_handle_events_timeout(self->ctx_, &tv);
        self->do_poll();   // re-arm incondizionato
    });
}
```

**Ottimizzazione**: tutto il polling USB avviene sul thread Boost.Asio già esistente — **zero thread aggiuntivi**. `set_poll_interval_ms()` permette di abbassare l'intervallo a 1ms su hardware lento o alzarlo a 50ms per ridurre CPU in idle.

### 1.2 Lifecycle

| Metodo | Azione |
|---|---|
| `initialize()` | `libusb_init(&ctx_)` + prima chiamata `do_poll()` |
| `stop()` | `timer_.cancel()` + `libusb_exit(ctx_)` + `ctx_ = nullptr` |
| `~LibusbContext()` | Chiama `stop()` automaticamente |
| `get_context_ptr()` | Ritorna `uintptr_t` del `libusb_context*` per passarlo a `USBWrapper` |

---

## 2. `external_bindings/aasdk_usb.hpp` — USB Hub e AOAP

### 2.1 Catena di creazione AOAP

```
LibusbContext.initialize()
    ↓ get_context_ptr()  →  USBWrapper(libusb_ctx_ptr)
                        →  AccessoryModeQueryFactory(wrapper, io_ctx_ptr)
                        →  AccessoryModeQueryChainFactory(wrapper, io_ctx_ptr, factory)
                        →  USBHub(wrapper, io_ctx_ptr, chain_factory)
                               └→ USBHub.start(io_ctx_ptr, on_ok, on_err)
                                       └→ on_ok(DeviceHandleHolder)
                                               └→ AOAPDevice.create(wrapper, io_ctx_ptr, holder)
```

Ogni step riceve `io_context_ptr` come `uintptr_t` e lo reinterpreta internamente — Python non gestisce mai puntatori raw.

### 2.2 `make_gil_safe_function()`

```cpp
inline std::shared_ptr<py::function> make_gil_safe_function(py::function fn) {
    return std::shared_ptr<py::function>(
        new py::function(std::move(fn)),
        [](py::function* p) {
            py::gil_scoped_acquire acquire;  // GIL obbligatorio per il distruttore
            delete p;
        });
}
```

**Ottimizzazione critica**: le callback Python (`on_ok`, `on_err`) vengono wrappate con un deleter custom che acquisisce il GIL prima di distruggere l'oggetto `py::function`. Senza questo, il distruttore potrebbe essere chiamato dal thread Asio senza GIL → segfault. Le callback stesse acquisiscono il GIL con `gil_scoped_acquire` prima di invocare Python.

### 2.3 `DeviceHandleHolder`

```cpp
struct DeviceHandleHolder {
    aasdk::usb::DeviceHandle handle;
};
```

Wrapper C++ attorno al `DeviceHandle` non copiabile. Python lo vede come oggetto opaco — lo passa solo ai metodi C++ che lo necessitano (`AOAPDevice::create`), senza mai accedere al contenuto.

---

## 3. `external_bindings/transport.hpp` — Transport Layer

Espone la catena di trasporto TLS/USB a Python:

| Classe | Ruolo |
|---|---|
| `SSLWrapper` | Wrapper OpenSSL (zero costruttore args) |
| `USBTransport` | I/O USB asincrono via Boost.Asio su AOAP |
| `ITransport` / `ISSLWrapper` | Interfacce base per polimorfismo pybind11 |

```python
# in TransportStack (app/core/transport_stack.py)
ssl = transport.SSLWrapper()
usb = transport.USBTransport(io_ctx_ptr, aoap_device)
```

`USBTransport` espone solo `stop()` a Python — tutta la logica I/O (lettura/scrittura messaggi AAP) avviene in C++ sul thread Asio.

---

## 4. `external_bindings/cryptor.hpp` — ICryptor (interfaccia)

Espone `ICryptor` come classe base per il polimorfismo Python, con i metodi TLS core:

| Metodo Python | Funzione |
|---|---|
| `do_handshake()` | Esegue un passo TLS handshake → `bool` (done?) |
| `write_handshake_buffer(bytes)` | Inietta dati TLS ricevuti dal telefono |
| `read_handshake_buffer()` | Legge dati TLS da inviare al telefono → `bytes` |

La conversione `py::bytes ↔ aasdk::common::DataConstBuffer` avviene tramite cast diretto del puntatore:

```cpp
std::string str = data;   // copia minima per lifetime safety
aasdk::common::DataConstBuffer buf(
    reinterpret_cast<const uint8_t*>(str.data()), str.size());
```

---

## 5. `external_bindings/messenger.hpp` — Cryptor concreto e Messenger

### 5.1 `Cryptor` (implementazione concreta)

Implementazione concreta di `ICryptor` su `SSLWrapper`. Metodi aggiuntivi rispetto all'interfaccia:

| Metodo | Descrizione |
|---|---|
| `init()` / `deinit()` | Setup/teardown SSL context (chiamati prima/dopo handshake) |
| `is_active()` | `True` dopo handshake completato con successo |

### 5.2 Stack completo Messenger

```python
# TransportStack assembly in Python
ssl      = transport.SSLWrapper()
usb      = transport.USBTransport(io_ctx_ptr, aoap_device)
cryptor  = messenger_mod.Cryptor(ssl)
cryptor.init()

in_stream  = messenger_mod.MessageInStream(io_ctx_ptr, usb, cryptor)
out_stream = messenger_mod.MessageOutStream(io_ctx_ptr, usb, cryptor)
msr        = messenger_mod.Messenger(io_ctx_ptr, in_stream, out_stream)
```

`Messenger` espone solo `stop()` a Python — viene passato a `ChannelManager` che lo usa per creare i channel objects C++.

### 5.3 Gerarchia ereditarietà pybind11

```
IMessageInStream  ←  MessageInStream
IMessageOutStream ←  MessageOutStream
IMessenger        ←  Messenger
ISSLWrapper       ←  SSLWrapper
ICryptor          ←  Cryptor
```

Le classi `I*` sono esposte come base class Python per permettere passaggi polimorfici — es. `ITransport::Pointer` come argomento ai costruttori degli stream.

---

## 6. `external_bindings/logger_binding.cpp` — Sistema di Logging aasdk

L'unico file `.cpp` non header-only del progetto (richiede una TU dedicata per il `PYBIND11_MODULE`).

### 6.1 Architettura dual-logger

Il sistema di logging del progetto è a **due livelli completamente indipendenti**:

```
app/core/py_logging.py                 ← Python standard logging
    get_logger("app.control.discovery")    → logging.Logger gerarchico
    get_logger("app.media.av_core")        → log da Python verso stdout/file

external_bindings/logger_binding.cpp   ← ModernLogger C++ di aasdk
    set_aasdk_log_level("WARN")            → sopprime log verbosi di aasdk
    set_aasdk_category_log_level("USB","DEBUG") → categoria specifica
```

La configurazione dei livelli avviene in `app/orchestrator.py` via `init_logging()` prima di avviare il runner.

### 6.2 Categorie disponibili (34 totali)

Le categorie coprono ogni sottosistema di aasdk:

| Gruppo | Categorie |
|---|---|
| Core | `SYSTEM`, `TRANSPORT`, `MESSENGER`, `PROTOCOL`, `GENERAL` |
| Connettività | `USB`, `TCP`, `WIFI`, `BLUETOOTH` |
| Media | `AUDIO`, `VIDEO`, `AUDIO_GUIDANCE`, `AUDIO_MEDIA`, `AUDIO_SYSTEM`, `AUDIO_TELEPHONY`, `AUDIO_MICROPHONE`, `VIDEO_SINK`, `VIDEO_CHANNEL` |
| Canali | `CHANNEL`, `CHANNEL_CONTROL`, `CHANNEL_BLUETOOTH`, `CHANNEL_MEDIA_SINK`, `CHANNEL_MEDIA_SOURCE`, `CHANNEL_INPUT_SOURCE`, `CHANNEL_SENSOR_SOURCE`, `CHANNEL_NAVIGATION`, `CHANNEL_PHONE_STATUS`, `CHANNEL_RADIO`, `CHANNEL_NOTIFICATION`, `CHANNEL_VENDOR_EXT`, `CHANNEL_WIFI_PROJECTION`, `CHANNEL_MEDIA_BROWSER`, `CHANNEL_PLAYBACK_STATUS` |

### 6.3 API Python

```python
import aasdk_logging

# Configurazione rapida (shortcut con valori default)
aasdk_logging.configure_aasdk_logging("TRACE", "DEBUG", "DEBUG")
# equivalente a: global=TRACE, USB=DEBUG, TCP=DEBUG

# Granulare
aasdk_logging.set_aasdk_log_level("WARN")
aasdk_logging.set_aasdk_category_log_level("VIDEO", "TRACE")
aasdk_logging.set_aasdk_category_log_level("USB", "ERROR")

# Diagnostica
logger = aasdk_logging.ModernLogger.instance()
print(logger.get_queue_size())        # backpressure del logger
print(logger.get_dropped_messages())  # messaggi persi per overflow
```

`parse_level()` e `parse_category()` accettano sia `str` che `int` (valore dell'enum) — API flessibile per scripting runtime senza import dell'enum.

---

## 7. `app/ui/main_window.py` — MainWindow

### 7.1 Struttura Layout

```
QMainWindow
└─ QWidget (root, contentsMargins=0)
   └─ QVBoxLayout (spacing=0)
      ├─ _header_container   stretch=0  ← HeaderModule
      ├─ _content_stack      stretch=1  ← QStackedWidget (pagine)
      └─ _footer_container   stretch=0  ← FooterModule
```

Ogni modulo gestisce internamente i propri `setContentsMargins` — il layout root non impone nulla.

### 7.2 Sistema moduli (plugin architecture)

`MainWindow` accetta una lista di `UIModule` e li distribuisce per `region`:

```python
self._modules = modules or self._default_modules()
# default: [HeaderModule, DisconnectedModule, AndroidAutoModule, ConfigModule, FooterModule]
```

`_build_modules()` smista:
- `region == "header"` → primo modulo → `_header_container`
- `region == "footer"` → primo modulo → chiama `configure(pages, nav_cb, action_cb, ...)` → `_footer_container`
- `region == "page"` → `QStackedWidget`, tab navigation

**Estensibilità**: aggiungere una nuova pagina richiede solo creare un `UIModule(region="page")` e passarlo alla lista — zero modifiche a `MainWindow`.

### 7.3 Temi e Font

Font stack con fallback progressivo: `["Montserrat", "Poppins", "Noto Sans", "DejaVu Sans"]` — garantisce rendering coerente su distribuzioni embedded senza Montserrat.

Il dark theme è definito come stringa CSS Qt inline con:
- `qlineargradient` sfondo `#0c141a → #1a2631`
- `objectName`-based selectors (es. `#HeaderTitle`, `#VideoSurface`, `#MicToast`)
- Tab selezionata via property: `QPushButton#TabButton[selected="true"]`

### 7.4 Overlay Toast

**MicToast** e **VolumeToast** sono `QLabel` posizionati con `move()` direttamente sulla `MainWindow`, fuori dal sistema di layout:

```python
# MicToast: 56×56px, rosso #d23b3b, border-radius: 28px (cerchio)
x = self.width() - 56 - 18
y = self._header_container.height() + 18
self._mic_toast.move(x, y)

# VolumeToast: sotto il MicToast, auto-hide 1500ms
self._volume_toast_timer.start(1500)  # singleShot
```

**Ottimizzazione**: `move()` su widget figlio è O(1), nessun reflow del layout. Entrambi chiamano `raise_()` per garantire che siano sempre sopra al content stack.

`resizeEvent()` ricalcola le posizioni di tutti gli overlay — supporto corretto per finestre ridimensionabili.

### 7.5 Animazione pagina (`_animate_page`)

```python
effect = QGraphicsOpacityEffect(widget)
widget.setGraphicsEffect(effect)
anim = QPropertyAnimation(effect, b"opacity", widget)
anim.setDuration(220)
anim.setEasingCurve(QEasingCurve.Type.OutCubic)  # fast start, smooth end
anim.start()
widget._fade_anim = anim   # strong ref: previene GC prematuro
```

`widget._fade_anim = anim` è essenziale: senza questa strong reference, il GC Python raccoglierebbe l'animazione prima della fine, causando un crash silenzioso.

### 7.6 Tastiera on-screen (`eventFilter`)

```python
def eventFilter(self, obj, event):
    if isinstance(obj, QLineEdit):
        if event.type() == FocusIn:
            self._keyboard.show_for(obj)
        elif event.type() == FocusOut:
            self._keyboard_hide_timer.start(250)  # grace period 250ms
```

Il timer da 250ms evita che la tastiera venga nascosta durante il click su un tasto (il `QLineEdit` perde focus per un istante quando il `QPushButton` del tasto lo riceve). L'`eventFilter` è installato sia su `self` che su `QApplication.instance()` per intercettare eventi da tutti i widget figli.

### 7.7 Key forwarding ad Android Auto

```python
def keyPressEvent(self, event):
    if self._input_enabled and not event.isAutoRepeat():
        self._input_frontend.send_key_down_qt(event.key())
```

`isAutoRepeat()` previene l'invio di eventi ripetuti da pressione prolungata — il protocollo AAP gestisce il longpress esplicitamente. Il `QMainWindow` cattura tutti i key event non consumati dai widget figli (nessun widget UI intercetta le frecce o i media keys).

---

## 8. `app/ui/modules/base.py` — UIModule

```python
@dataclass
class ModuleDescriptor:
    name: str
    region: str  # "header" | "footer" | "page"

class UIModule:
    def build(self, parent=None) -> QWidget:
        raise NotImplementedError
```

Pattern **Strategy + Factory**: `MainWindow` dipende solo dall'interfaccia `UIModule`, non dalle implementazioni concrete. Ogni modulo:
- Riceve `parent` in `build()` per creare widget con ownership corretta
- Gestisce internamente timer, state, connessioni signal/slot
- Non ha riferimenti a `MainWindow` — comunicazione solo via callback

---

## 9. `app/ui/modules/header.py` — HeaderModule

Header minimalista: titolo istanza (sinistra) + orologio `HH:mm` (destra). Il `QTimer(1000ms)` è figlio del `root` widget — viene distrutto automaticamente quando il widget viene rimosso, senza memory leak.

```python
timer = QTimer(root)   # root è parent → ownership automatica
timer.timeout.connect(_tick)
timer.start()
_tick()   # primo aggiornamento immediato (evita "--:--" per 1 secondo)
```

---

## 10. `app/ui/modules/footer.py` — FooterModule

Footer con tre zone orizzontali:

```
[VOL+][VOL-][ASSIST][MUTE]  |  [VOL ====slider==== 50%]  |  [Home][AA][Config]
         azioni primarie              slider volume                 tab nav
```

**`blockSignals` per sync volume**:
```python
slider.blockSignals(True)    # evita loop setValue→valueChanged→set_volume→...
slider.setValue(value)
slider.blockSignals(False)
```

`configure()` è chiamato da `MainWindow._build_modules()` prima di `build()`, iniettando i callback di navigazione e azione — separazione netta tra setup e rendering.

---

## 11. `app/ui/modules/disconnected.py` — DisconnectedModule

Schermata idle con orologio hero **56pt bold** e linee di stato configurabili. Default:
```python
info_lines = ["Nessun dispositivo collegato", "USB pronto — in attesa"]
```

Come `HeaderModule`, il `QTimer` è figlio del widget root per lifetime automatico.

---

## 12. `app/ui/modules/settings.py` — SettingsModule

Form di configurazione con `QGridLayout` 2 colonne (label + input) per:
- Nome istanza → `QLineEdit`
- Modalità USB/WiFi → `QComboBox`
- BT Address → `QLineEdit` con placeholder `"00:11:22:33:44:55"`

Tutti i label usano `setObjectName("ConfigLabel")` e tutti gli input `setObjectName("ConfigInput")` per il CSS theme centralizzato in `MainWindow._app_stylesheet()`.

---

## 13. `app/ui/modules/keyboard.py` — OnScreenKeyboard

Tastiera on-screen `4 righe × 10 tasti + bottom row` con `QGridLayout`.

### Layout

```
[1][2][3][4][5][6][7][8][9][0]
[Q][W][E][R][T][Y][U][I][O][P]
[A][S][D][F][G][H][J][K][L]
[Z][X][C][V][B][N][M]
[   Space   ][⌫][Clear][Enter][Hide]
```

**`setFocusPolicy(NoFocus)`** su tutti i tasti: i click non rubano il focus al `QLineEdit` target, preservando il cursore di testo durante la digitazione.

**Target condiviso**: un'unica istanza `OnScreenKeyboard` serve tutti i `QLineEdit` dell'app — `show_for(target)` aggiorna semplicemente `self._target`.

Azioni speciali:
| Tasto | Azione |
|---|---|
| `SPACE` | `target.insert(" ")` |
| `BACKSPACE` | `target.backspace()` |
| `CLEAR` | `target.clear()` |
| `ENTER` | `text_committed.emit()` + `self.hide()` |
| `HIDE` | `self.hide()` |

---

## 14. Pattern Globali e Osservazioni Architetturali

### 14.1 Il Boundary C++/Python è una linea rigida

```
┌─────────────────────────────────────────────────────────┐
│  PYTHON (GIL presente)                                  │
│  UI PyQt6, Config, Handler logic, Wiring canali         │
│                                                         │
│  ↕  Protobuf control messages (decine di byte, rari)   │
│  ↕  Notifiche evento (ACK, setup, open, error)         │
├─────────────────────────────────────────────────────────┤  ← boundary GIL
│  C++ (GIL assente per media)                            │
│  Boost.Asio I/O loop, GStreamer pipeline                │
│  H.264 frame buffer, PCM/AAC/OPUS audio buffer          │
│  JitterBuffer lock-free, AvCore pipeline threads        │
└─────────────────────────────────────────────────────────┘
```

I flussi media (audio/video) non attraversano mai il boundary — vengono gestiti interamente in C++ da `AvCore`. Solo i messaggi di controllo (protobuf piccoli e rari) attraversano via `EventBinding.call()` con GIL acquisito per la sola durata della chiamata.

### 14.2 `uintptr_t` come Handle Opaco Universale

Tutti gli oggetti C++ condivisi cross-layer usano `uintptr_t`:

| Handle | Usato da |
|---|---|
| `io_context_ptr` | strand, hub, transport, messenger, audio/video handler |
| `av_core_ptr` | `AudioEventHandler`, `VideoEventHandler` |
| `libusb_context_ptr` | `USBWrapper` |

Questo pattern garantisce:
- **Type erasure**: Python non può chiamare metodi C++ senza passare per un binding esplicito
- **Thread safety**: l'`uintptr_t` è un intero copiabile — nessun problema di ownership
- **No GIL**: i puntatori possono essere usati in C++ anche quando il GIL è rilasciato

### 14.3 Re-arm Asincrono Universale

Ogni handler, in ogni condizione (successo, errore, no-op), termina con:

```python
channel.receive(handler)
```

È il pattern Boost.Asio di **async perpetuo**. La mancanza di questo `receive()` in una singola branch congelerebbe silenziosamente il canale — il bug si manifesterebbe come "canale smette di rispondere" senza nessun errore esplicito.

### 14.4 Smart Pointer e Lifetime Cross-Layer

| Pattern | Dove | Garanzia |
|---|---|---|
| `shared_ptr` per tutti i C++ objects | tutti i binding | Nessun dangling pointer |
| `py::keep_alive` implicito via `shared_ptr` | pybind11 | Python non GC-izza oggetti ancora usati da C++ |
| `make_gil_safe_function` per callback | `aasdk_usb.hpp` | GIL acquisito nel distruttore `py::function` |
| `widget._fade_anim = anim` | `main_window.py` | Strong ref impedisce GC dell'animazione in esecuzione |
| `QTimer(root)` come parent | tutti i moduli UI | Timer distrutto con il widget, zero leak |

### 14.5 Configurabilità Zero-Ricompilazione

Il progetto espone una superficie di configurazione runtime completa senza ricompilare:

| Meccanismo | Esempi |
|---|---|
| `~/.nemoheadunit/config.json` | risoluzione, codec audio, sink GStreamer, BT address |
| Variabili d'ambiente C++ | `NEMO_AUDIO_SINK`, `NEMO_VIDEO_DECODER`, `NEMO_AUDIO_DUMP` |
| Variabili d'ambiente Python | `NEMO_AUDIO_FORCE_PCM`, `NEMO_AUDIO_FORCE_UNIFORM_FORMAT` |
| `aasdk_logging` Python API | livelli di log per categoria a runtime |
| `LibusbContext.set_poll_interval_ms()` | tuning USB polling senza ricompilazione |

### 14.6 Flusso Completo End-to-End (Telefono → Schermo)

```
[Telefono Android]
    ↓ USB AOAP
[LibusbContext.do_poll()]          ← Boost.Asio timer, thread C++
    ↓
[USBTransport] → [Cryptor TLS] → [Messenger]
    ↓                              decodifica frame AAP
[Channel (es. MEDIA_SINK_VIDEO)]
    ↓
[VideoEventHandler C++]            ← nessun GIL
    ├─ EventBinding.call("OnMediaWithTimestamp")
    │     ↓ GIL acquisito
    │  [VideoEventHandlerLogic.on_media_with_timestamp (Python)]
    │     └─ channel.send_ack()
    │     ↓ GIL rilasciato
    └─ AvCore::pushVideo(frame_data)   ← nessun GIL
            ↓
       [video_queue_ deque]  ←→  [video_thread_]
            ↓
       [GstVideoPipeline appsrc]
            ↓
       [h264parse → avdec_h264 → videoconvert → xvimagesink]
            ↓
       [VideoSurface.winId() (X11 handle)]   ← QWidget nativo, GStreamer overlay
            ↓
[Schermo 800×480]
```

Il frame H.264 percorre l'intero stack **senza mai toccare Python** dopo la ricezione USB — solo il piccolo ACK protobuf passa per il GIL.

---

## 15. Riepilogo Ottimizzazioni — Vista Unica su tutti i Chunk

| Tecnica | File | Beneficio |
|---|---|---|
| `do_poll` ricorsivo su timer Asio | `usb_context.hpp` | Zero thread USB extra |
| `make_gil_safe_function` custom deleter | `aasdk_usb.hpp` | Nessun segfault nel distruttore callback |
| `DeviceHandleHolder` opaco | `aasdk_usb.hpp` | Python non accede a handle USB direttamente |
| Reinterpret cast zero-copia per TLS buffer | `cryptor.hpp` | Nessuna copia dei buffer SSL |
| `shared_ptr` per tutta la gerarchia C++ | tutti i binding | Lifetime automatico cross-layer |
| `uintptr_t` handle pattern | tutti i binding | Type-safe cross-layer, no GIL needed |
| GIL acquisito solo nella `call()` | `event_binding.hpp` | I/O USB + GStreamer fuori dal GIL |
| `camel_to_snake` automatico | `event_binding.hpp` | Zero mapping esplicito C++→Python |
| `GetProtobuf` factory C++ | `protobuf_message.hpp` | Python non importa header protobuf media |
| Dual logger indipendente | `logger_binding.cpp` | Controllo separato log aasdk vs app |
| 34 categorie log granulari | `logger_binding.cpp` | Debug mirato senza rumore |
| `paintEngine()→None` | `android_auto.py` | GStreamer su X11 nativo, zero copia pixel |
| `WA_SynthesizeMouseForUnhandledTouchEvents=False` | `android_auto.py` | No double-event touch+mouse |
| Pointer ID mapping stabile | `android_auto.py` | IDs consistenti per multi-touch AAP |
| Anti-ghost 200ms | `android_auto.py` | No click fantasma da sintesi mouse |
| `TouchCancel` → ACTION_UP per tutti | `android_auto.py` | Nessun pointer stuck sul telefono |
| `move()` per toast overlay | `main_window.py` | O(1), nessun reflow layout |
| `widget._fade_anim = anim` strong ref | `main_window.py` | Nessun GC dell'animazione in esecuzione |
| `QTimer(root)` parent-owned | tutti i moduli UI | Timer distrutto con widget, zero leak |
| `blockSignals` per slider volume | `footer.py` | Nessun loop setValue→valueChanged |
| `setFocusPolicy(NoFocus)` su tasti | `keyboard.py` | Focus preservato sul QLineEdit target |
| `isAutoRepeat()` check | `main_window.py` | Nessun key repeat verso AAP |
| Plugin architecture UIModule | `base.py` + `main_window.py` | Nuove pagine senza modificare MainWindow |
| `leaky=downstream` su queue GStreamer | `av_core.hpp` | Drop frame vecchi, no stall pipeline |
| `sync=false` sui GStreamer sink | `av_core.hpp` | Latenza controllata dall'app |
| Soft mixer `int32_t` clipping | `av_core.hpp` | Zero dipendenze audio, bassa CPU |
| Resampling lineare inline | `av_core.hpp` | O(N), zero dipendenze esterne |
| Strand per canale AV | `channel_manager.py` | Ordinamento FIFO, zero lock condivisi |
| GIL release su start/stop runner | `io_context_runner.hpp` | Nessuna contesa thread Asio vs UI |
| `threading.Timer` daemon per USB restart | `orchestrator.py` | Recovery automatico, no blocco main thread |
| Enum fuzzy resolution | `service_discovery.py` | Config user-friendly senza nomi completi |
| `NEMO_AUDIO_FORCE_PCM/UNIFORM` | `service_discovery.py` | Debug compatibilità senza ricompilare |
| `apply_audio_env()` propagazione config | `config.py` | C++ legge config via env, no IPC |

---

*Fine analisi — `codebase_analyze_chunk_1.md` + `codebase_analyze_chunk_2.md` + `codebase_analyze_chunk_3.md` coprono l'intera codebase di NemoHeadUnit.*
