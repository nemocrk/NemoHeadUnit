# NemoHeadUnit — Analisi Codebase · Chunk 2/3

> **Generato automaticamente** · 2026-03-17  
> Copertura: `app/orchestrator.py`, `handlers/audio/`, `handlers/video/`, `handlers/input/`, `handlers/control/control_modules/service_discovery.py`, `handlers/common/event_binding.hpp`, `handlers/common/protobuf_message.hpp`, `external_bindings/channels.hpp`, `app/ui/modules/android_auto.py`, tutti gli altri handler

---

## 1. `app/orchestrator.py` — Composition Root

L'`Orchestrator` è il **composition root** dell'intera applicazione: crea, cablava e gestisce il lifecycle di tutti i componenti runtime.

### 1.1 Lifecycle dettagliato

```
Orchestrator.__init__()         ← riceve: screen_size, bt_config, channels list, runner, av_core, keycodes
    ↓
start_runner()                  ← init_logging() + IoContextRunner.start() (thread C++ Boost.Asio)
    ↓
start_usb()                     ← crea UsbHubManager, registra on_device + _on_error
    ↓                               (thread Boost.Asio gestisce tutto il resto)
create_on_device_handler()      ← closure eseguita su plug AOAP
    ├── hub.create_aoap_device(handle)
    ├── create_transport_stack(aoap)       → TransportStack (SSL + USB + Cryptor + Messenger)
    └── create_channel_manager(messenger, cryptor)
            ├── _build_handlers()          → dict {channel_id → *EventHandlerLogic}
            └── ChannelManager.start()
                    └── channel.receive(handler) per ogni canale
```

### 1.2 `_build_handlers()` — handler factory

Itera la lista `channels` configurata in `main.py` e, per ogni `ChannelId` abilitato, istanzia il corrispondente `*EventHandlerLogic`. Gli handler sono **singleton per sessione**: un solo `AudioEventHandlerLogic` serve tutti e tre i canali audio (media, guidance, system). `VideoOrchestrator` riceve il callback `on_active_changed` che notifica la UI.

```python
if ch_id == video_id:
    video_logic = VideoEventHandlerLogic(
        orchestrator=VideoOrchestrator(on_active_changed=self._on_aa_active_changed)
    )
```

### 1.3 Auto-restart USB

In caso di errore (dispositivo scollegato, timeout, eccezione nel setup):

1. `_on_error()` / eccezione in `on_device()` chiama `_restart_usb(1.0)`
2. Teardown ordinato: `ChannelManager.stop()` → `TransportStack.stop()` → `hub.hard_teardown()`
3. `threading.Timer(1.0, _do_restart)` (daemon thread) ritenta `start_usb()`
4. Se il retry fallisce ancora, ricursione con lo stesso `delay_s`

**Ottimizzazione**: il `threading.Timer` è daemon, non blocca lo shutdown dell'applicazione. Il flag `_stopping` previene restart durante lo shutdown volontario.

### 1.4 `stop()` — graceful shutdown

Sequenza ordinata per evitare dangling pointer o segfault cross-layer:

```
request_shutdown()      ← invia ByeByeRequest AAP
sleep(0.2)              ← grace period per il telefono
ChannelManager.stop()
hub.stop()
TransportStack.stop()
av_core.stop()
runner.stop()
hub.hard_teardown()     ← libusb cleanup forzato
```

---

## 2. `handlers/audio/audio_event_handler.py` — Canale Audio

### 2.1 `AudioOrchestrator`

Responsabile delle risposte protobuf per il canale audio. Interno al file, non dipende da moduli esterni. Segue il principio di **separazione della risposta dalla logica di dispatch**:

| Callback | Risposta protobuf |
|---|---|
| `on_media_channel_setup_request` | `AVChannelConfig(STATUS_READY, max_unacked=1)` |
| `on_channel_open_request` | `ChannelOpenResponse(STATUS_SUCCESS)` |
| `on_media_channel_start` | nessuna (solo setta session_id) |
| `on_media_with_timestamp` | `MediaAck(session_id, ack=1)` |
| `on_channel_error` | hook logging/telemetria |

### 2.2 `AudioEventHandlerLogic`

Dispatcher degli eventi AAP per il canale audio. Ogni metodo:
1. Estrae `channel` e `strand` dal `handler`
2. Delega la costruzione della risposta all'orchestratore
3. Invia la risposta via C++ (`channel.send_*`)
4. Chiama `channel.receive(handler)` per il **re-arm asincrono**

### 2.3 Gestione `MediaWithTimestamp` vs `MediaIndication`

Entrambi i metodi seguono la stessa logica di ACK:
- `on_media_with_timestamp_indication`: frame con timestamp esplicito → `TRACE` log
- `on_media_indication`: frame senza timestamp → `DEBUG` log (fallback)

In entrambi i casi il frame audio **non viene toccato in Python**: il C++ handler (lato `audio_event.hpp`) passa il payload direttamente a `AvCore::pushAudio()` tramite `av_core_ptr`, bypassando completamente il GIL.

---

## 3. `handlers/video/video_event_handler.py` — Canale Video

### 3.1 Sequenza di setup canale

```
1. AVChannelSetupRequest  →  AVChannelConfig(STATUS_READY, max_unacked=1)
                         →  [callback _after_setup]
                         →  VideoFocusNotification(VIDEO_FOCUS_PROJECTED, unsolicited=False)
2. ChannelOpenRequest    →  ChannelOpenResponse(STATUS_SUCCESS)
3. VideoFocusRequest     →  VideoFocusNotification(VIDEO_FOCUS_PROJECTED)
4. MediaChannelStart     →  (salva session_id, on_active_changed(True))
5. MediaWithTimestamp    →  MediaAck(session_id, ack=1) + pushVideo() in C++
6. MediaChannelStop      →  on_active_changed(False), reset session_id
```

### 3.2 Nota critica sul path media

`on_media_indication` è esplicitamente commentato come `no-op: avoid heavy processing in Python`. I frame H.264 vengono gestiti interamente dal handler C++ (`video_event.hpp`) che chiama `AvCore::pushVideo()` direttamente. Python riceve solo la notifica per l'ACK, non i dati video.

### 3.3 Debug layer

I metodi includono un doppio layer di logging:
- Livello `TRACE`: log di ogni frame con `payload_size`, `session_id`, dettagli ACK
- Il parse del `MediaAck` in Python avviene **solo per debug** (in un `try/except`), non per la logica di ACK che avviene in C++

---

## 4. `handlers/input/input_event_handler.py` — Canale Input

### 4.1 `InputOrchestrator`

Gestisce il negotiation dei keycodes con il telefono:
- Se la HeadUnit ha `supported_keycodes` configurati, verifica che i keycodes richiesti dal telefono siano un sottoinsieme.
- Se la richiesta contiene keycodes non supportati → `STATUS_KEYCODE_NOT_BOUND`.
- Mantiene `_bound_keycodes` per l'invio degli `InputReport`.

### 4.2 `default_supported_keycodes()`

Lista statica dei 19 keycodes default supportati:
```
KEYCODE_HOME, KEYCODE_BACK, KEYCODE_CALL, KEYCODE_ENDCALL,
KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT/CENTER, KEYCODE_ENTER, KEYCODE_MENU,
KEYCODE_MEDIA_PLAY_PAUSE, KEYCODE_MEDIA_NEXT, KEYCODE_MEDIA_PREVIOUS,
KEYCODE_VOLUME_UP, KEYCODE_VOLUME_DOWN, KEYCODE_VOLUME_MUTE,
KEYCODE_VOICE_ASSIST, KEYCODE_SCROLL_LOCK
```

### 4.3 `InputEventHandlerLogic` — Frontend API

Espone un'API completa per l'invio di eventi touch e keyboard al telefono:

| Metodo | Protobuf generato |
|---|---|
| `send_touch_down(x, y, pid)` | `InputReport → TouchEvent(ACTION_DOWN)` |
| `send_touch_up(x, y, pid)` | `InputReport → TouchEvent(ACTION_UP)` |
| `send_touch_move(x, y, pid)` | `InputReport → TouchEvent(ACTION_MOVED)` |
| `send_touch(action, points, idx)` | `InputReport → TouchEvent(multi-pointer)` |
| `send_key(keycode, down, meta)` | `InputReport → KeyEvent.Key` |

Il timestamp è generato con `time.time_ns() // 1000` (microsecondi) per massima precisione.

### 4.4 `InputFrontend` — adapter PyQt6

Thin wrapper che aggiunge la conversione `Qt.Key → AAP KeyCode` tramite `build_pyqt6_keycode_map()`.

La mappa copre:
- Lettere A-Z, cifre 0-9
- Navigazione (DPAD, Enter, Backspace, Escape, Tab, Home, End, PageUp/Down)
- Media keys (Play, Pause, Stop, Next, Previous, Volume±, Mute)
- Android specifics (Back, Menu)

Metodi frontend:
- `send_key_down_qt(qt_key)` → `send_key(mapped_keycode, True)`
- `send_key_up_qt(qt_key)` → `send_key(mapped_keycode, False)`

---

## 5. `handlers/common/event_binding.hpp` — EventBinding

Il **nucleo del bridge C++→Python** per tutti gli event handler.

### 5.1 Meccanismo `call()`

```cpp
template <typename THandler, typename TData>
void call(THandler* self, const char* method, const TData& data) {
    pybind11::gil_scoped_acquire acquire;   // acquisisce il GIL
    const std::string py_method = camel_to_snake(method);  // converte nome
    
    // Serializza protobuf → bytes se TData è MessageLite
    if constexpr (std::is_base_of_v<MessageLite, TData>)
        data.SerializeToString(&payload);
    
    py_impl_.attr(py_method.c_str())(cast(self), bytes(payload));
}
```

**Ottimizzazione chiave**: il GIL viene acquisito **solo per la durata della chiamata Python**. Tutto il processing C++ (I/O USB, parsing AAP, deserializzazione protobuf) avviene fuori dal GIL.

### 5.2 `camel_to_snake()`

Conversione automatica del nome del metodo C++ (CamelCase) in Python (snake_case):
- `OnMediaChannelSetupRequest` → `on_media_channel_setup_request`
- `OnChannelOpenRequest` → `on_channel_open_request`

Eliminando mapping espliciti, ogni nuovo metodo C++ è automaticamente instradato al metodo Python corrispondente.

### 5.3 `Strand` binding

```cpp
.def("dispatch", [](strand& s, py::function fn) {
    s.dispatch([fn = move(fn)]() mutable {
        py::gil_scoped_acquire acquire;
        fn();
    });
});
```

Permette di fare dispatch di callable Python sul thread Asio (utile per operazioni UI thread-safe).

---

## 6. `handlers/common/protobuf_message.hpp` — ProtobufMessage

Factory C++ per messaggi protobuf dinamici, esposta a Python.

### 6.1 `GetProtobuf(type_name)`

```cpp
auto* desc = DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
auto* proto = MessageFactory::generated_factory()->GetPrototype(desc);
return make_shared<ProtobufMessage>(unique_ptr<Message>(proto->New()));
```

Permette a Python di creare istanze protobuf per full type name senza includere header:

```python
cfg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
cfg.parse_from_string(response_bytes)
channel.send_channel_setup_response(cfg, strand, tag)
```

**Ottimizzazione**: la serializzazione/deserializzazione avviene sempre in C++. Python non manipola mai i bytes raw dei protobuf media (solo quelli di controllo/setup, che sono rari e piccoli).

---

## 7. `external_bindings/channels.hpp` — ChannelId e Channel Services

### 7.1 Enum `ChannelId`

Espone l'enum `aasdk::messenger::ChannelId` a Python con tutti i 20 canali AAP:

| Canale | ID | Tipo |
|---|---|---|
| `CONTROL` | 0 | Setup/TLS/Discovery |
| `SENSOR` | 1 | Sensori veicolo |
| `MEDIA_SINK_VIDEO` | 3 | Video H.264 |
| `MEDIA_SINK_MEDIA_AUDIO` | 4 | Audio media |
| `MEDIA_SINK_GUIDANCE_AUDIO` | 5 | Audio navigazione |
| `MEDIA_SINK_SYSTEM_AUDIO` | 6 | Audio sistema |
| `MEDIA_SINK_TELEPHONY_AUDIO` | 7 | Audio telefonia |
| `INPUT_SOURCE` | 8 | Touch/keyboard |
| `MEDIA_SOURCE_MICROPHONE` | 9 | Microfono |
| `BLUETOOTH` | 10 | BT pairing/WiFi projection |
| `PHONE_STATUS` | 11 | Stato chiamata |
| `NAVIGATION_STATUS` | 12 | Navigazione turn-by-turn |
| `MEDIA_PLAYBACK_STATUS` | 13 | Stato playback media |
| `MEDIA_BROWSER` | 14 | Browser libreria media |
| `VENDOR_EXTENSION` | 15 | Estensioni vendor |
| `GENERIC_NOTIFICATION` | 16 | Notifiche generiche |
| `WIFI_PROJECTION` | 17 | Proiezione wireless |
| `RADIO` | 18 | Sintonizzatore radio |

### 7.2 Service classes esposte

- `VideoMediaSinkService`: accetta `(strand, messenger, channel_id)` → `shared_ptr`
- `SensorSourceService`: accetta `(strand, messenger)`
- `InputSourceService`: accetta `(strand, messenger)`
- `NavigationStatusService`: accetta `(strand, messenger)`

---

## 8. `handlers/control/control_modules/service_discovery.py` — Service Discovery

### 8.1 `build_service_discovery_response()`

Funzione che costruisce il protobuf `ServiceDiscoveryResponse` completo, dichiarando tutte le capacità della HeadUnit al telefono. È il punto in cui la configurazione runtime si traduce in capacità AAP negoziate.

### 8.2 Canali dichiarati nella risposta

| CH ID | Canale | Configurazione chiave |
|---|---|---|
| 1 | SENSOR | Sensori: DRIVING_STATUS, LOCATION, NIGHT_MODE |
| 3 | VIDEO | Codec H264_BP, risoluzione 800×480, 30fps, density da config |
| 4 | MEDIA_AUDIO | Codec da config (PCM/AAC/OPUS), sample_rate/bits/channels |
| 5 | GUIDANCE_AUDIO | Come media, tipo AUDIO_STREAM_GUIDANCE |
| 6 | SYSTEM_AUDIO | Come media, tipo AUDIO_STREAM_SYSTEM_AUDIO |
| 8 | INPUT_SOURCE | TouchScreen CAPACITIVE width×height, keycodes supportati |
| 9 | MIC | PCM/AAC, sample_rate/bits/channels |
| 10 | BLUETOOTH | Solo se `bluetooth_available=True`, metodi pairing PIN+NUMERIC |
| 11 | PHONE_STATUS | Status chiamate |
| 12 | NAVIGATION | IMAGE 256×256 16bit, interval 1000ms |
| 13 | MEDIA_PLAYBACK_STATUS | Stato playback |
| 14 | MEDIA_BROWSER | Browser |
| 15 | VENDOR_EXTENSION | Estensioni vendor |
| 16 | GENERIC_NOTIFICATION | Notifiche |
| 17 | WIFI_PROJECTION | Proiezione WiFi |
| 18 | RADIO | Radio |

### 8.3 Override via variabili d'ambiente

| Env Var | Effetto |
|---|---|
| `NEMO_AUDIO_FORCE_PCM=1` | Forza codec PCM per tutti gli stream audio |
| `NEMO_AUDIO_FORCE_UNIFORM_FORMAT=1` | Allinea sample_rate/bits/channels di tutti gli stream al canale media |

### 8.4 Enum resolution robusta

`_select_enum_value()` implementa una ricerca fuzzy dell'enum protobuf:
1. Match esatto (case insensitive)
2. Substring match
3. Match dopo stripping dei prefissi standard (`MEDIA_CODEC_VIDEO_`, `VIDEO_`, ecc.)

Permette di usare valori di config come `"H264"` invece di `"MEDIA_CODEC_VIDEO_H264_BP"`.

### 8.5 Headunit info

```python
msg.headunit_info.make = "NemoDev"
msg.headunit_info.model = "NemoHU"
msg.headunit_info.head_unit_software_version = "0.1.0"
msg.display_name = "NemoHeadUnit"
msg.driver_position = DRIVER_POSITION_LEFT
```

Ping configuration: `tracked=5, timeout=3000ms, interval=1000ms, high_latency_threshold=200ms`.

---

## 9. `app/ui/modules/android_auto.py` — VideoSurface e Touch

### 9.1 `VideoSurface(QWidget)`

Widget nativo che funge da superficie di rendering GStreamer + input touch proxy.

**Attributi critici:**
```python
setAttribute(WA_NativeWindow, True)    # forza creazione handle nativo (WId)
setAttribute(WA_AcceptTouchEvents, True)  # abilita eventi touch nativi
setAttribute(WA_SynthesizeMouseForUnhandledTouchEvents, False)  # disabilita sintesi mouse da touch
```

**Ottimizzazione `paintEngine() → None`**: override che disabilita il paint engine Qt. GStreamer usa `gst_video_overlay_set_window_handle(wid)` per disegnare direttamente sul WId X11, bypassing completamente il sistema di paint di Qt. Zero copia di pixel.

### 9.2 `_map_to_target(pos)`

Conversione coordinate widget → coordinate video target con preservazione aspect ratio:

```python
scale = min(widget_w / target_w, widget_h / target_h)
video_w = target_w * scale
video_h = target_h * scale
off_x = (widget_w - video_w) / 2.0     # letterboxing orizzontale
off_y = (widget_h - video_h) / 2.0     # letterboxing verticale

x = int((px - off_x) / video_w * target_w)
y = int((py - off_y) / video_h * target_h)
```

I tocchi fuori dall'area video (letterbox) vengono ignorati (`return None`). Coordinate risultanti clampate a `[0, target_w-1] × [0, target_h-1]`.

### 9.3 Gestione touch multi-pointer

Sistema di mapping dei pointer ID raw Qt → pointer ID stabili AAP:

```python
# Assegna pointer ID stabile 0,1,2,... ai raw ID del sistema
mapped_id = self._pointer_id_map.get(raw_pid)
if mapped_id is None:
    mapped_id = self._next_pointer_id
    self._next_pointer_id += 1
    self._pointer_id_map[raw_pid] = mapped_id
```

Logica azioni multi-pointer:
- Prima pressione di un gesto: `ACTION_DOWN`
- Pressioni aggiuntive: `ACTION_POINTER_DOWN`
- Rilascio parziale: `ACTION_POINTER_UP`
- Rilascio ultimo dito: `ACTION_UP`
- `action_index`: indice del pointer che ha scatenato l'evento nella lista ordinata per ID

**Reset automatico**: quando tutti i pointer sono rilasciati, la mappa ID viene azzerata (`_pointer_id_map.clear()`), così il prossimo gesto ricomincia da ID 0.

### 9.4 Anti-ghost mouse

`_should_ignore_mouse()` filtra gli eventi mouse sintetici generati dal sistema quando viene usato il touch:
- Se l'ultimo touch è stato meno di 200ms fa → ignora
- Se `event.source()` è `MouseEventSynthesizedBySystem` o `MouseEventSynthesizedByQt` → ignora

### 9.5 `TouchCancel` handling

```python
if event.type() == QEvent.Type.TouchCancel:
    self._handle_touch_cancel()  # invia ACTION_UP per tutti i pointer attivi
```

Garantisce che il telefono non rimanga con pointer "stuck" in caso di interruzione del gesto (es. popup di sistema).

---

## 10. Panoramica degli Handler Secondari

### 10.1 Pattern comune

Tutti i 14 handler secondari (oltre audio, video, input, control) seguono il medesimo pattern:

```python
class *EventHandlerLogic:
    def on_channel_open_request(self, handler, payload):
        # risponde con ChannelOpenResponse(STATUS_SUCCESS)
        channel.send_channel_open_response(resp, strand, tag)
        channel.receive(handler)  # re-arm
    
    def on_*_request/indication(self, handler, payload):
        # parsing protobuf, logica specifica
        channel.receive(handler)  # re-arm
    
    def on_channel_error(self, handler, payload):
        _logger.error(...)
        channel.receive(handler)  # re-arm anche in caso di errore
```

### 10.2 Handler specifici notevoli

| Handler | Evento chiave | Risposta / Azione |
|---|---|---|
| `SensorEventHandlerLogic` | `SensorBatchRequest` | Invia `SensorBatch` con dati velocità/GPS/nightmode |
| `BluetoothEventHandlerLogic` | `BluetoothPairingRequest` | Gestisce PIN e numeric comparison per WiFi projection |
| `NavigationEventHandlerLogic` | `NavigationStatusUpdate` | Log turn-by-turn, immagini navigazione |
| `MediaSourceEventHandlerLogic` | `ChannelOpen` | Abilita cattura microfono, `AvCore.start_mic()` |
| `MediaPlaybackStatusEventHandlerLogic` | `MediaPlaybackStatusUpdate` | Aggiorna stato playback nella UI |
| `PhoneStatusEventHandlerLogic` | `PhoneStatusUpdate` | Log chiamata in corso, aggiorna footer UI |
| `RadioEventHandlerLogic` | `RadioUpdate` | Log frequenza/RDS corrente |
| `VendorExtensionEventHandlerLogic` | `VendorExtensionData` | Forward generico per estensioni OEM |
| `WifiProjectionEventHandlerLogic` | `WifiProjectionStatus` | Gestisce la transizione USB→WiFi |
| `GenericNotificationEventHandlerLogic` | `GenericNotification` | Bridge notifiche app → UI toast |
| `MediaBrowserEventHandlerLogic` | `MediaBrowseRequest` | Risponde con lista media items |

---

## 11. Riepilogo ottimizzazioni specifiche di questo chunk

| Tecnica | File | Beneficio |
|---|---|---|
| GIL acquisito solo nella `call()` | `event_binding.hpp` | Tutto il processing I/O avviene fuori dal GIL |
| `camel_to_snake` automatico | `event_binding.hpp` | Zero mapping esplicito metodo C++→Python |
| `GetProtobuf` via factory C++ | `protobuf_message.hpp` | Nessun import di header protobuf in Python per i canali media |
| `paintEngine() → None` | `android_auto.py` | GStreamer disegna direttamente su X11, zero copia pixel |
| `WA_SynthesizeMouseForUnhandledTouchEvents = False` | `android_auto.py` | Elimina double-event touch+mouse |
| Pointer ID mapping stabile | `android_auto.py` | IDs consistenti per multi-touch AAP |
| Anti-ghost 200ms | `android_auto.py` | No false click da eventi mouse sintetici |
| `threading.Timer` daemon per restart USB | `orchestrator.py` | Recovery automatico senza bloccare il main thread |
| Handler singleton per sessione | `orchestrator.py` | Un solo oggetto Python per 3 canali audio |
| Enum fuzzy resolution | `service_discovery.py` | Config user-friendly senza nomi enum completi |
| `NEMO_AUDIO_FORCE_PCM` / `FORCE_UNIFORM` | `service_discovery.py` | Debug/compatibilità senza ricompilare |

---

*Continua in `codebase_analyze_chunk_3.md` (external_bindings rimanenti, UI modules restanti, scripts, pattern globali)*
