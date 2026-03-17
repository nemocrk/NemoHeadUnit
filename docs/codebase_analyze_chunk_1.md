# NemoHeadUnit — Analisi Codebase · Chunk 1/3

> **Generato automaticamente** · 2026-03-17  
> Copertura: `CMakeLists.txt`, `app/main.py`, `app/orchestrator.py`, `app/config.py`, `app/core/channel_manager.py`, `external_bindings/io_context_runner.hpp`, `external_bindings/transport.hpp`, `app/media/av_core.hpp`, `handlers/control/control_event_handler.py`

---

## 1. Visione d'Insieme

NemoHeadUnit è un **emulatore open-source di Android Auto HeadUnit**, progettato per girare su hardware con risorse limitate (Intel Atom Z3770 x86_64, Raspberry Pi ARM). L'architettura segue un pattern ibrido **C++17 ↔ Python 3** mediato da `pybind11`:

- **C++** detiene tutto ciò che è real-time o latency-critical: I/O USB asincrono (Boost.Asio), TLS/SSL (OpenSSL via `aasdk`), pipeline media GStreamer, jitter-buffer audio lock-free.
- **Python** gestisce la logica applicativa, la wiring dei canali, la configurazione e l'intera UI (PyQt6).
- I flussi multimediali (H.264 video, PCM/AAC/OPUS audio) **non attraversano mai il GIL**: vengono iniettati direttamente in GStreamer dal lato C++ tramite puntatori opachi (`uintptr_t`) passati cross-layer.

---

## 2. Stack Build (`CMakeLists.txt`)

### 2.1 Dipendenze esterne

| Libreria | Versione minima | Ruolo |
|---|---|---|
| `Boost::system` | qualsiasi | Boost.Asio event loop |
| `protobuf::libprotobuf` | qualsiasi | Serializzazione protobuf AAP |
| `OpenSSL::SSL/Crypto` | qualsiasi | Handshake TLS Android Auto |
| `libusb-1.0` | qualsiasi | Accesso diretto USB (AOAP) |
| `gstreamer-1.0` | ≥ 1.18 | Pipeline media core |
| `gstreamer-app-1.0` | ≥ 1.18 | GstAppSrc / GstAppSink |
| `gstreamer-video-1.0` | ≥ 1.18 | GstVideoOverlay (embed in QWidget) |
| `pybind11` | v3.0.2 | Binding C++ ↔ Python |
| `aasdk` | `main` (opencardev) | Protocollo Android Auto |

### 2.2 Pattern `create_pybind_module`

Il `CMakeLists.txt` definisce una funzione CMake helper che **genera automaticamente il wrapper TU** (translation unit) per ogni modulo pybind11:

```cmake
create_pybind_module(<TARGET> <INIT_FUNC>
    HEADER <header.hpp>
    [SOURCES <src1.cpp> ...]
)
```

Viene generato in `CMAKE_CURRENT_BINARY_DIR/<TARGET>_wrapper.cpp` che include il header indicato e invoca `INIT_FUNC` dal `PYBIND11_MODULE`. Questo permette di avere **header-only implementations** per la maggior parte dei binding (es. `av_core.hpp`, `io_context_runner.hpp`, tutti i handler), senza .cpp separati.

### 2.3 Moduli pybind11 prodotti

| Modulo Python | Header sorgente | Dipendenze extra |
|---|---|---|
| `aasdk_logging` | `external_bindings/logger_binding.cpp` | `aasdk` |
| `protobuf` | `handlers/common/protobuf_message.hpp` | `protobuf` |
| `event` | `handlers/common/event_binding.hpp` | `Boost, Threads` |
| `io_context` | `external_bindings/io_context_runner.hpp` | `Boost, Threads` |
| `cryptor` | `external_bindings/cryptor.hpp` | `aasdk, Boost` |
| `transport` | `external_bindings/transport.hpp` | `aasdk, libusb, Boost` |
| `messenger` | `external_bindings/messenger.hpp` | `aasdk, OpenSSL, Boost` |
| `channels` | `external_bindings/channels.hpp` | `aasdk, Boost` |
| `aasdk_usb` | `external_bindings/aasdk_usb.hpp` | `aasdk, libusb, Boost` |
| `av_core` | `app/media/av_core.hpp` | `GStreamer, aasdk, Boost` |
| `audio_event` | `handlers/audio/audio_event_handler.hpp` | `GStreamer, aasdk` |
| `video_event` | `handlers/video/video_event_handler.hpp` | `GStreamer, aasdk` |
| `sensor_event` | `handlers/sensor/...hpp` | `aasdk, Boost` |
| `input_event` | `handlers/input/...hpp` | `aasdk, Boost` |
| `navigation_event` | `handlers/navigation/...hpp` | `aasdk, Boost` |
| `control_event` | `handlers/control/...hpp` | `aasdk, Boost` |
| `bluetooth_event` | `handlers/bluetooth/...hpp` | `aasdk, Boost` |
| + altri 8 handler | vari | `aasdk, Boost` |

---

## 3. Entrypoint: `app/main.py`

`main()` è il punto di avvio e si occupa di:

1. **Configurare `AvCore`** — istanzia `av_core.AvCore(cfg)` con un `AvCoreConfig` letto da `app/config.py`. Configura risoluzione video (800×480), gli stream audio per canale (media, guidance, system, mic), le politiche overrun/underrun e la priorità audio.
2. **Definire i canali** — lista esplicita di `ChannelId` con strand dedicati per i canali AV (video, media_audio, guidance_audio, system_audio, microphone).
3. **Avviare `Orchestrator`** — con `orch.start_runner()` (lancia il thread Boost.Asio) e `orch.start_usb()` (avvia la discovery USB).
4. **Avviare la UI PyQt6** — `MainWindow(800, 480)`, installazione del `_TouchDebugFilter` (event filter touch-to-overlay), gestione SIGINT/SIGTERM con `QTimer.singleShot(0, app.quit)`.
5. **QTimer di polling** (300ms) — binding lazy di `InputFrontend`, `window_id` GStreamer e sincronizzazione stato AA attivo/disconnesso.
6. **QTimer volume** (2000ms) — sincronizza il volume di sistema alla config e mostra toast.

### Ottimizzazioni chiave in `main.py`

- **`py::call_guard<py::gil_scoped_release>()`** usato nei binding C++ per `start`/`stop` del runner: il GIL viene rilasciato durante l'esecuzione del thread Asio, eliminando contesa.
- **Strand dedicati per canali AV**: ogni canale audio/video ha il proprio `io_context::strand`, garantendo ordinamento FIFO senza lock condivisi.
- **Lazy binding del `window_id`**: GStreamer viene avviato solo *dopo* che il QWidget è visibile e ha un native handle (`WId`), evitando race condition all'avvio.

---

## 4. `app/orchestrator.py` — Orchestrator

L'`Orchestrator` è il *composition root* dell'applicazione: crea e cablava tutti i componenti runtime.

```
Orchestrator
├── IoContextRunner      (thread C++ Boost.Asio)
├── UsbHubManager        (discovery USB AOAP, callback on_device)
│   └── on_device() →
│       ├── create_transport_stack(aoap)  → TransportStack
│       └── create_channel_manager(...)  → ChannelManager
│           └── start_channels()
└── stop()               (graceful shutdown, hard_teardown USB)
```

### Lifecycle

| Metodo | Azione |
|---|---|
| `start_runner()` | `init_logging()` + `IoContextRunner.start()` |
| `start_usb()` | Crea `UsbHubManager`, registra callback `on_device` e `_on_error` |
| `create_on_device_handler()` | Closure che, al plug di un dispositivo AOAP, crea transport + channel manager |
| `_build_handlers()` | Istanzia tutti i `*EventHandlerLogic` per i canali abilitati |
| `stop()` | `request_shutdown` → `ChannelManager.stop()` → `hub.hard_teardown()` → `runner.stop()` |
| `_restart_usb(delay_s)` | Teardown completo + `threading.Timer` → `start_usb()` dopo `delay_s` secondi |

### Gestione errori

L'`Orchestrator` implementa un **auto-restart USB**: in caso di errore durante la connessione (es. dispositivo scollegato), esegue un teardown completo di channel manager, transport e hub, poi ritenta con un `threading.Timer(1.0)`. Questo evita loop stretti sul main thread.

---

## 5. `app/config.py` — Configurazione

Singola fonte di verità per tutta la configurazione runtime. Usa `dataclass` con serializzazione JSON (`~/.nemoheadunit/config.json`).

### Struttura `AppConfig`

```
AppConfig
├── UiConfig          (instance_name, mode USB/WiFi, bt_address)
├── ServiceDiscoveryConfig
│   ├── enabled_channels: List[int]
│   ├── audio_streams: Dict[channel_id, AudioStreamConfig]
│   └── video: VideoConfig (codec, resolution, frame_rate, density)
├── LoggingConfig     (core_level, aasdk_level)
└── AudioOutputConfig
    ├── sink           (GStreamer sink, es. "pulsesink", "autoaudiosink")
    ├── buffer_ms      (100ms default)
    ├── max_queue_frames, mic_frame_ms, mic_batch_ms
    ├── volume_backend ("pactl" | "wpctl")
    ├── volume_device, volume_step, volume_percent, muted
```

### Variabili d'ambiente esposte

`apply_audio_env()` propaga la config nel processo tramite env vars leggibili dai pipeline C++:

| Env Var | Significato |
|---|---|
| `NEMO_AUDIO_SINK` | GStreamer audio sink override |
| `NEMO_AUDIO_BUFFER_MS` | Buffer playback in ms |
| `NEMO_VIDEO_DECODER` | Override decoder video (default: `avdec_h264 max-threads=2`) |
| `NEMO_VIDEO_SINK` | Override video sink (default: `xvimagesink`) |
| `NEMO_MIC_SRC` | Override sorgente microfono (default: `autoaudiosrc`) |
| `NEMO_AUDIO_DUMP` | Path per dump raw/encoded degli stream audio in entrata |
| `NEMO_AUDIO_DUMP_SINGLE` | `1`/`true` = file unico; altrimenti per-stream |

### Gestione volume

Supporto per due backend intercambiabili:
- **`pactl`** (PulseAudio): `pactl set-sink-volume`/`get-sink-volume`
- **`wpctl`** (WirePlumber/PipeWire): `wpctl set-volume`/`get-volume`

---

## 6. `app/core/channel_manager.py` — ChannelManager

Il `ChannelManager` è il *wiring layer* che collega i moduli Python ai binding C++ per ogni canale AAP.

### Pattern Strand

Ogni canale AV riceve il proprio `Strand` dedicato:
```python
strand = _event.Strand(io_context_ptr)  # nuovo strand per AV
# oppure
strand = self._default_strand            # strand condiviso per canali non-AV
```

I canali AV (video, media audio, guidance audio, system audio, microphone) sono configurati con strand separati dall'`Orchestrator` tramite `channel_strands` dict, garantendo **isolamento dei flussi** senza lock.

### Pattern EventBinding

```python
binding = _event.EventBinding(logic)  # wrappa la Python logic in un C++ callback
handler = _*_event.*EventHandler(strand, channel, binding [, av_core_ptr])
channel.receive(handler)
```

Il meccanismo `EventBinding` è il ponte fondamentale: il codice C++ chiama la callback Python corrispondente all'evento ricevuto sul canale. Passare l'`av_core_ptr` (puntatore intero opaco all'istanza C++ di `AvCore`) ai VideoEventHandler e AudioEventHandler permette al C++ di chiamare direttamente `pushVideo`/`pushAudio` **senza attraversare il GIL**.

### Ciclo vita canali

1. `start()` — crea tutti i channel objects e chiama `channel.receive(handler)` + `send_version_request` per il canale di controllo.
2. `stop()` — chiama `ch.stop()` su ogni canale aperto.
3. `request_shutdown()` — invia `ByeByeRequest` protobuf prima dello stop.

---

## 7. `external_bindings/io_context_runner.hpp` — IoContextRunner

Wrapper C++ attorno a `boost::asio::io_context` esposto a Python:

```cpp
class IoContextRunner {
    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<...> work_guard_;  // impedisce stop prematuro
    std::thread thread_;  // thread dedicato al loop Asio
};
```

**Ottimizzazione**: `start`/`stop` esposti con `py::call_guard<py::gil_scoped_release>()` — il GIL viene rilasciato durante queste chiamate bloccanti, permettendo al thread Python di continuare. Il `work_guard_` mantiene `io_context_.run()` attivo fino alla chiamata esplicita di `stop()`.

---

## 8. `app/media/av_core.hpp` — AvCore (il cuore media)

`AvCore` è la classe C++ più complessa del progetto. Gestisce tre pipeline GStreamer indipendenti + un mixer audio software.

### 8.1 Struttura

```
AvCore
├── GstVideoPipeline     video_pipeline_
├── GstAudioPipeline     audio_pipeline_
├── GstMicCapture        mic_capture_
│
├── std::deque<AvFrame>  video_queue_    (mutex + condition_variable)
├── unordered_map<int, deque<AvFrame>>  audio_queues_  (per stream_id)
├── std::deque<AvFrame>  mic_queue_
│
├── std::thread          video_thread_
├── std::thread          audio_thread_
└── std::thread          mic_thread_
```

### 8.2 GstVideoPipeline

Pipeline testuale GStreamer costruita a runtime:
```
appsrc → queue(leaky=downstream, max=8) → h264parse → avdec_h264(max-threads=2)
       → videoconvert → xvimagesink
```
- `sync=false` sul sink elimina il clock GStreamer come source di latenza.
- `leaky=downstream` sulla queue: in caso di backpressure, scarta i frame *più vecchi*, prevenendo stallamenti visivi.
- L'overlay è iniettato via `GstVideoOverlay::set_window_handle(wid)` dal `window_id` del QWidget nativo.

### 8.3 GstAudioPipeline

Pipeline selezionata dinamicamente in base al codec:

| Codec | Pipeline |
|---|---|
| PCM | `appsrc → queue(leaky) → audioconvert → audioresample → sink` |
| AAC_LC | `appsrc → queue → avdec_aac → audioconvert → audioresample → sink` |
| OPUS | `appsrc → queue → opusparse ! opusdec → audioconvert → audioresample → sink` |

Capacità dinamica: `setCodecData()` aggiorna i caps `appsrc` a runtime (es. AAC codec_data estratti dai primi 2 byte del primo frame), con flag `caps_dirty_` atomico per aggiornamento thread-safe.

### 8.4 GstMicCapture

```
autoaudiosrc → audioconvert → audioresample
             → audio/x-raw,S16LE,chN,rateN
             → identity(do-timestamp=true)
             → audiobuffersplit(output-buffer-duration=frame_ms)
             → queue(max=8, leaky=downstream)
             → appsink(sync=false, max-buffers=4, drop=true)
```

Se `audiobuffersplit` non è disponibile, fallback automatico senza split.

### 8.5 Jitter Buffer e Politiche

#### Video Jitter Buffer
```cpp
const int min_frames = max(1, cfg_.jitter_buffer_ms / 33);  // @30fps
// attende min_frames prima di emettere
```

#### Audio Prebuffer (per codec encoded)
```cpp
int prebuffer_frames = (cfg_.audio_prebuffer_ms + frame_ms - 1) / frame_ms;
// attende accumulo prima di iniziare la riproduzione
```

#### Politiche Overrun
- `DROP_OLD`: rimuove il frame più vecchio dalla coda (default).
- `DROP_NEW`: scarta il frame appena arrivato.
- `PROGRESSIVE_DISCARD`: svuota la coda al 50% della capacità massima.

#### Politica Underrun
- `SILENCE`: inietta frame di silenzio (zero bytes) per evitare glitch audio.
- `WAIT`: blocca finché non arrivano dati.

### 8.6 Mixer Audio Software (PCM)

Per il path PCM, `AvCore` implementa un **soft mixer prioritizzato**:

1. `AudioGroup` definisce gruppi di canali con priorità, ducking (%) e hold_ms.
2. `pickTopGroupLocked()` sceglie il gruppo con priorità più alta che ha dati disponibili.
3. `mixGroupLocked()` somma i sample in `int32_t` con clipping a `[-32768, 32767]`.
4. I canali a priorità inferiore vengono mixati con gain ridotto (`ducking/100`).

Esempio configurazione da `main.py`:
```python
av.set_audio_priority([
    ([GUIDANCE_AUDIO, SYSTEM_AUDIO], priority=100, ducking=100, hold_ms=120),
    ([MEDIA_AUDIO],                  priority=50,  ducking=50,  hold_ms=40),
])
```
La voce di navigazione (GUIDANCE) sopprime il media audio al 50% del volume.

### 8.7 Normalizzazione PCM cross-stream

`normalizePcmFrame()` esegue **channel conversion** e **resampling lineare** software per uniformare stream con sample rate o canali diversi al formato output:
- Mono→Stereo: duplicazione del campione.
- Stereo→Mono: media dei due canali.
- Resampling: interpolazione lineare `O(N)` senza dipendenze esterne.

### 8.8 A/V Sync (Master Clock)

Il `videoLoop` controlla la deriva A/V:
```cpp
if (frame.ts_us > audio_ts + max_av_lead_ms * 1000) {
    sleep_for(2ms);  // attende che l'audio recuperi
}
```
`max_av_lead_ms` (default 80ms) è la tolleranza massima di anticipo del video sull'audio.

### 8.9 Timestamp tracking

`nextAudioTimestamp()` mantiene un timestamp monotono per stream:
- Se `last == 0`: usa il timestamp preferito o `1 * frame_step`.
- Altrimenti: `last + step`, oppure `preferred_ts` se è maggiore.
- Sincronizza il timestamp globale `last_audio_ts_` atomico per il comparatore video.

---

## 9. `handlers/control/control_event_handler.py` — Canale di Controllo

Il canale di controllo implementa il protocollo di handshake Android Auto:

```
VersionRequest →  VersionResponse
Handshake TLS  →  HandshakeResponse (più iterazioni)
AuthComplete   →  ServiceDiscoveryRequest → ServiceDiscoveryResponse
PingRequest    →  PingResponse
AudioFocusReq  →  AudioFocusNotification
NavFocusReq    →  NavFocusNotification
VoiceSessionReq → VoiceSessionNotification
ByeByeRequest  →  ByeByeResponse
```

- `HandshakeState` (in `control_modules/handshake.py`) gestisce lo stato della TLS state machine.
- `build_service_discovery_response()` costruisce il protobuf `ServiceDiscoveryResponse` con i canali abilitati, la configurazione video/audio e i keycodes supportati.
- Ogni callback chiama `channel.receive(handler)` dopo la risposta: pattern **async re-arm** tipico di Boost.Asio.

---

## 10. Riepilogo Ottimizzazioni Architetturali

| Tecnica | Dove | Beneficio |
|---|---|---|
| GIL release su `start`/`stop` | `io_context_runner.hpp` | Nessuna contesa thread |
| Strand per canale AV | `channel_manager.py` | Ordinamento FIFO, zero lock condivisi |
| `uintptr_t` per `AvCore` | `av_core.hpp` + `channel_manager.py` | Media data path senza GIL |
| `leaky=downstream` queue GStreamer | `av_core.hpp` | Drop frame vecchi, no stall pipeline |
| `sync=false` sui sink | `av_core.hpp` | Latenza controllata dall'app, non dal clock GStreamer |
| `std::once_flag` per `gst_init` | `av_core.hpp` | Inizializzazione GStreamer thread-safe una sola volta |
| `std::atomic<bool>` per running flags | `av_core.hpp` | Check running senza lock |
| Soft mixer con `int32_t` clipping | `av_core.hpp` | Nessuna dipendenza da librerie audio, bassa CPU |
| Resampling lineare inline | `av_core.hpp` | Zero dipendenze, predictable CPU |
| Audio dump via env var | `av_core.hpp` | Debug non-invasivo in produzione |
| `pybind11` `keep_alive` implicito via `shared_ptr` | tutti i binding | Nessun dangling pointer cross-layer |

---

*Continua in `codebase_analyze_chunk_2.md` (handlers, UI, external_bindings, scripts)*
