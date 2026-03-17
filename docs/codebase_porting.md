# NemoHeadUnit — Codebase & Porting (C++ ⇄ Python)

Data analisi: 2026-03-11

Obiettivo di questo documento:
1) rappresentare la codebase (moduli, responsabilità, flussi);
2) per **ogni metodo/entrypoint rilevante** in `src/**` e `python/**` (escluso `python/aasdk_proto/**`), descrivere cosa fa;
3) indicare se la **logica** può vivere in Python mantenendo il C++ come *thin binding/adapter* verso librerie native, oppure se esiste una parte che deve restare C++ (per motivi tecnici/realtime).

> Nota: in questa revisione “portabile in Python” significa **portare in Python la logica/policy**, lasciando in C++ solo:
> - le chiamate a librerie native (aasdk/libusb/asio/gstreamer),
> - gli adapter richiesti da interfacce C++ (callback/virtual),
> - i path hot (buffer video/audio) dove non vogliamo attraversare il confine C++↔Python.
>
> Con questa strategia, **quasi tutto è “portabile” come logica**: il C++ resta ma si svuota (forwarding + plumbing).

---

## Stato lavori (binding-first)

Implementato in questo repo (incrementale):
- ModernLogger: aggiunte API Python per configurare `aasdk::common::ModernLogger` (logica spostata a Python, C++ thin wrapper).
- IoContextRunner: esposto `is_running()` via binding (lifecycle guidabile da Python).
- LibusbContext: esposto via binding (costruzione da `IoContextRunner`, start/stop, poll interval configurabile).
- SessionManager: esposto via binding (metodi `start/stop/enable_video_dump`) e ottenibile da Python tramite `UsbHubManager.get_session_manager()`.
- EventHandler “svuotati” (parziale, incremental): `SensorEventHandler`, `InputEventHandler`, `NavigationEventHandler` ora delegano a nuovi hook opzionali su `IOrchestrator` (Python) con fallback C++; `AudioEventHandler` e `VideoEventHandler` aggiungono hook non-hot start/stop.
- Error reporting: tutti gli handler chiamano `IOrchestrator.onChannelError(source, message)` se presente (Python: `on_channel_error(source: str, message: str)`).
- Interfaccia unica (fallback): `PyOrchestrator` supporta `handle_event(name: str, channel_id: int, payload: bytes) -> bytes|None` come fallback quando un metodo specifico non esiste.
- Pattern “Python fa send/receive”: introdotto `AudioChannelContext` (binding) e hook orchestrator per demandare a Python `send_*` e `receive()` nel setup/open audio.

Non ancora completato (target prossimi):
- Hot path: media buffer (video/audio) restano in C++; per “svuotarli” ulteriormente serve definire quali metadati/comandi esporre a Python senza attraversare i buffer.
- Hook stats/telemetria: aggiungere callback Python per contatori (NAL count, bytes dump, ecc.) senza passare i buffer.

---

## 1) Panoramica architetturale

**Librerie principali (C++):**
- **aasdk**: implementa gran parte dello stack Android Auto (canali, messenger, cryptor, USB transport).
- **Boost.Asio**: event loop e serializzazione del lavoro (strand) per la parte realtime.
- **libusb**: discovery e gestione eventi USB.
- **GStreamer**: decodifica e rendering H.264; alimentato via `appsrc`.
- **pybind11**: bridge C++ ↔ Python.

**Ruolo del Python in questa repo:**
- implementa “logica di orchestrazione” (costruzione messaggi protobuf, handshake pilotato, UI e glue code),
- configura/avvia la catena C++ tramite il modulo `nemo_head_unit` compilato in `build/`,
- gestisce i certificati TLS in puro Python (`python/app/crypto_manager.py`) e li passa al C++ come stringhe PEM.

**Flusso runtime (alto livello):**
1) Python crea `IoContextRunner` (thread Asio C++).
2) Python crea `UsbHubManager`, setta orchestrator (Python) e cert/key (PEM).
3) C++ fa discovery USB (libusb + aasdk USBHub).
4) Alla connessione: C++ crea `USBTransport` + `Cryptor` + `Messenger` + `SessionManager`.
5) I vari *EventHandler* C++ ricevono messaggi protobuf da Android e delegano al Python orchestrator quando serve una risposta.
6) Video: i NAL H.264 non passano mai in Python; vanno a `GstVideoSink` → `GstPipeline` → rendering.

---

## 2) Criteri pratici per decidere “binding-first”

Deve **restare in C++** (anche solo come plumbing) se:
- è in path “hot” (buffer video/audio, callback frequenti, timing stretto),
- dipende da librerie native (libusb, GStreamer, aasdk internals),
- richiede threading preciso e non può rischiare blocchi di GIL.

È **portabile in Python (logica)** se:
- decide policy (risposte ai request, configurazione canali, gating),
- fa serializzazione protobuf e business rules,
- gestisce config/lifecycle/UI e I/O non realtime,
- orchestra “cosa fare” lasciando al C++ “come parlare con la libreria”.

---

## 3) Analisi metodi C++ (`src/**`) con obiettivo “svuotare logica”

### `src/binding.cpp`

- `hello_world()`: stampa un messaggio di test per verificare il binding.
  - Logica portabile in Python: **Sì (banale)**. Utile però restare in C++ come smoke test del modulo compilato.
- `enable_aasdk_logging()`: abilita `aasdk::common::ModernLogger` e setta livelli/categorie.
  - Logica portabile in Python: **Sì (configurazione)** tramite le funzioni binding (C++ resta solo bridge verso ModernLogger).
- `PYBIND11_MODULE(nemo_head_unit, m)`: definisce le classi/func esposte a Python (`IoContextRunner`, `UsbHubManager`, `GstVideoSink`, `ICryptor` + helpers).
  - Logica portabile in Python: **N/A** (è il binding stesso). Obiettivo: aumentare API verso Python e ridurre decisioni in C++.

### `src/bindings/logger_binding.cpp`

- `init_logger_bindings(m)`: registra funzioni Python per configurare ModernLogger.
  - API esposte: `configure_aasdk_logging(...)`, `set_aasdk_log_level(level)`, `set_aasdk_category_log_level(category, level)`.
  - Logica portabile in Python: **Sì** (Python decide livelli/categorie), C++ resta wrapper verso `aasdk::common::ModernLogger`.

### `src/core/io_context_runner.hpp` (`nemo::IoContextRunner`)

- `IoContextRunner::IoContextRunner()`: crea `io_context` e `work_guard`.
  - Logica portabile in Python: **Sì (lifecycle/uso)**, ma l’implementazione resta C++ perché aasdk usa Boost.Asio.
- `IoContextRunner::~IoContextRunner()`: chiama `stop()`.
  - Logica portabile in Python: **Sì (policy di stop)**, implementazione C++ (RAII).
- `start()`: avvia `io_context_.run()` in un worker thread.
  - Logica portabile in Python: **Sì** (quando avviare/fermare), implementazione C++.
- `stop()`: ferma work_guard + stop io_context + join thread.
  - Logica portabile in Python: **Sì**, implementazione C++.
- `is_running()`: indica se il worker thread è stato avviato (flag locale).
  - Logica portabile in Python: **Sì** (utility), implementazione C++ minimale.
- `get_io_context()`: espone reference a `io_context_`.
  - Logica portabile in Python: **No** (handle interno C++). In binding-first si evita di esporre dettagli: meglio esporre API ad alto livello.

### `src/usb/libusb_context.hpp|.cpp` (`nemo::LibusbContext`)

- `LibusbContext::LibusbContext(io_context)`: inizializza `usb_context_` e `steady_timer`.
  - Logica portabile in Python: **Sì** (frequenza polling, start/stop), implementazione C++ (libusb+Asio).
- `LibusbContext::~LibusbContext()`: cancella timer e fa `libusb_exit`.
  - Logica portabile in Python: **Sì (policy)**, implementazione C++.
- `initialize()`: `libusb_init`, avvia polling non bloccante.
  - Logica portabile in Python: **Sì** (policy/retry/error handling), implementazione C++.
- `stop()`: cancella il timer di polling (non fa `libusb_exit`, quello resta nel distruttore).
  - Logica portabile in Python: **Sì** (lifecycle), implementazione C++.
- `set_poll_interval_ms(ms)`: rende configurabile il polling interval (prima fisso a 10ms).
  - Logica portabile in Python: **Sì**.
- `is_initialized()`: indica se `libusb_init` è andato a buon fine.
  - Logica portabile in Python: **Sì**.
- `do_poll()`: timer Asio ogni ~10ms, chiama `libusb_handle_events_timeout`.
  - Logica portabile in Python: **No (hot-ish)**: il polling resta C++, ma si può rendere configurabile (periodo) via binding.

### `src/usb/usb_hub_manager.hpp|.cpp` (`nemo::UsbHubManager`)

- `UsbHubManager::UsbHubManager(runner)`: salva reference a `IoContextRunner`.
  - Logica portabile in Python: **Sì** (il “manager” può essere visto come un handle), ma C++ deve costruire oggetti aasdk.
- `~UsbHubManager()`: chiama `stop()`.
  - Logica portabile in Python: **Sì** (policy), implementazione C++ (RAII/teardown).
- `start(callback)`: inizializza `LibusbContext`, `USBWrapper`, `QueryFactory/ChainFactory`, `USBHub`, poi `post(startDiscovery)`.
  - Logica portabile in Python: **Sì** (configurazione: callback, retry, timeouts, logging); implementazione C++ resta come wrapper.
- `stop()`: teardown ordinato di sessione/transport/cryptor/usb hub.
  - Logica portabile in Python: **Sì** (quando e perché stoppare), teardown resta C++.
- `setOrchestrator(shared_ptr<IOrchestrator>)`: imposta orchestrator (di solito `PyOrchestrator`).
  - Logica portabile in Python: **Sì** (orchestrator è Python). C++ resta solo per tenere la reference.
- `setCertificateAndKey(cert,key)`: memorizza PEM stringhe.
  - Logica portabile in Python: **Sì** (caricamento/validazione/sorgente). C++ conserva solo i bytes.
- `setVideoSink(sink)`: propaga `GstVideoSink` alla sessione.
  - Logica portabile in Python: **Sì** (quando/che sink), implementazione C++ (plumbing).
- `enableVideoDump(path)`: delega a `SessionManager` se esiste o salva `pending_dump_path_`.
  - Logica portabile in Python: **Sì** (policy). Il dump su buffer resta C++.
- `getSessionManager()`: espone il `SessionManager` creato dopo la connessione (può essere `nullptr` finché non c’è device).
  - Logica portabile in Python: **Sì** (consente a Python di pilotare `SessionManager`), implementazione C++.
- `startDiscovery()`: avvia discovery su `usb_hub_` con promise.
  - Logica portabile in Python: **Sì** (policy: start/stop/retry), implementazione C++ (promise aasdk).
- `onDeviceDiscovered(handle)`: costruisce AOAPDevice, USBTransport, SSLWrapper, Cryptor; scrive PEM su disco; crea Messenger + SessionManager e avvia sessione; invoca callback Python.
  - Logica portabile in Python: **Sì (quasi tutta)**: gestione “cosa fare quando connesso” può diventare callback/evento verso Python.  
    Implementazione C++ necessaria per: costruire oggetti aasdk, mantenere ownership, garantire ordine teardown, path hot.
- `onDiscoveryFailed(error)`: log + callback Python.
  - Logica portabile in Python: **Sì** (reporting/retry policy). C++ resta adapter callback.
- `ensureCertificatesExist()`: crea `./cert/` e scrive `headunit.crt/key` su disco.
  - Logica portabile in Python: **Sì** (ideale spostarla in Python). Vincolo: `aasdk::Cryptor::init()` legge da filesystem, quindi o:
    - Python scrive i file prima di `start()`; oppure
    - si estende il C++ con un binding “write_cert_files(cert,key,dir)” ma la decisione resta Python.

### `src/session/session_manager.hpp` (`nemo::SessionManager`)

- `SessionManager::SessionManager(...)`: salva strand, messenger, cryptor, orchestrator, sink.
  - Logica portabile in Python: **Sì (configurazione/feature flags)**, ma la composizione canali resta C++ (aasdk).
- `makePromise(tag)`: crea send promise e logga errore sul `then`.
  - Logica portabile in Python: **Sì (logging policy)**, implementazione C++.
- `enableVideoDump(path)`: dispatch su strand; se `video_handler_` esiste abilita dump, altrimenti salva `pending_dump_path_`.
  - Logica portabile in Python: **Sì**, implementazione C++ perché deve sincronizzarsi sullo strand e toccare handler.
- `start()`: costruisce canali (Control/Sensor/Video/Audio/Input/Nav), crea handler, `receive()` loop e invia `VersionRequest`.
  - Logica portabile in Python: **Sì (quali canali abilitare, config, hook di eventi)**, ma il wiring `receive()` e la creazione canali restano C++.
- `stop()`: reset canali/handler via dispatch su strand.
  - Logica portabile in Python: **Sì (policy)**, implementazione C++.

### `src/session/control_event_handler.hpp` (`nemo::ControlEventHandler`)

- `makePromise(tag)`: promise con log OK/FAIL.
  - Logica portabile in Python: **Sì (policy logging/telemetry)**, implementazione C++.
- `onVersionResponse(major,minor,status)`: chiede a orchestrator Python il primo chunk TLS (flight 1) e lo invia via `sendHandshake`.
  - Logica portabile in Python: **Sì (già)**. C++ resta come adapter callback aasdk→Python.
- `onHandshake(payload)`: passa chunk a orchestrator Python e decide se inviare altro handshake o `AuthComplete`.
  - Logica portabile in Python: **Sì** (policy “se inviare altro/finire”). C++ resta come adapter di I/O su canale.
- `onServiceDiscoveryRequest(req)`: delega a orchestrator, parse response, invia.
  - Logica portabile in Python: **Sì** (protobuf/policy). C++ deve solo inviare su channel e gestire receive loop.
- `onAudioFocusRequest(req)`, `onNavigationFocusRequest(req)`, `onVoiceSessionRequest(req)`, `onBatteryStatusNotification(req)`, `onByeByeRequest(req)`, `onPingRequest(req)` (+ response handlers/error):
  - Porting in Python: **No** (callback C++), ma **tutta la policy** può stare in Python orchestrator (come già fatto).
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Control", msg)` se presente.
  - Logica portabile in Python: **Sì** (telemetria/policy di recovery).

### `src/session/video_event_handler.hpp` (`nemo::VideoEventHandler`)

- `makePromise(tag)`: promise con log.
  - Porting in Python: **No**.
- `onMediaChannelSetupRequest(setup)`, `onChannelOpenRequest(open)`, `onVideoFocusRequest(vf)`: delegano al Python orchestrator per le risposte/gate; inviano le response.
  - Logica portabile in Python: **Sì** (già). C++ resta adapter callback.
- `onMediaWithTimestampIndication(ts,buffer)` / `onMediaIndication(buffer)`: ricevono NAL units e passano a `processBuffer`.
  - Logica portabile in Python: **No (hot path)**. Questo è uno dei pochi punti che devono restare C++ “pieno”.
- `enableDump(path)`: apre file dump e abilita contatori.
  - Porting in Python: **Possibile ma sconsigliato** se il dump è effettuato nel path hot; meglio resti nel thread C++ che riceve i buffer.
- `processBuffer(ts,buffer)`: logging NAL, dump fino a 5MB, push verso GStreamer (`video_sink_->pushBuffer`).
  - Logica portabile in Python: **Parziale** (solo policy/telemetry). La manipolazione buffer e push GStreamer restano C++.
- `sendVideoFocusIndication()`: invia `VIDEO_FOCUS_PROJECTED`.
  - Porting in Python: **Sì (logica)**, ma oggi è comodo in C++ per gating immediato.
- `onMediaChannelStartIndication(start)` / `onMediaChannelStopIndication(stop)`: oltre al comportamento esistente, invocano hook Python non-hot (`onVideoChannelStart/Stop`) se presente.
  - Logica portabile in Python: **Sì (non-hot)**.
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Video", msg)` se presente.
  - Logica portabile in Python: **Sì**.

### `src/session/audio_event_handler.hpp` (`nemo::AudioEventHandler`)

- `makePromise(tag)`: promise log.
  - Porting in Python: **No**.
- `onMediaChannelSetupRequest`, `onChannelOpenRequest`: delegano al Python orchestrator per le risposte.
  - Logica portabile in Python: **Sì (full send/receive)** tramite i nuovi hook:
    - `on_audio_media_channel_setup_request(channel_id: int, payload: bytes, ctx: AudioChannelContext) -> None`
    - `on_audio_channel_open_request(channel_id: int, payload: bytes, ctx: AudioChannelContext) -> None`
    Python usa `ctx.send_setup_response(...)`, `ctx.send_open_response(...)`, `ctx.receive()`.
    Fallback: se i metodi non esistono, `PyOrchestrator` usa il flow legacy `on_av_channel_setup_request` / `on_channel_open_request`.
- `onMediaChannelStartIndication`, `onMediaChannelStopIndication`: oggi solo log; futuro start/suspend sink audio.
  - Porting in Python: **Dipende**: se il sink è native (ALSA/Pulse/pipewire), meglio C++; se solo policy, Python.
- `onMediaChannelStartIndication` / `onMediaChannelStopIndication`: invocano hook Python non-hot (`onAudioChannelStart/Stop`) se presente.
  - Logica portabile in Python: **Sì (non-hot)**.
- `onMediaWithTimestampIndication(ts,buffer)`, `onMediaIndication(buffer)`: oggi “drop + Ack”; futuro jitter buffer.
  - Logica portabile in Python: **Sì (policy)** ma i buffer realtime/jitter buffer restano C++.
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Audio CHx", msg)` se presente.
  - Logica portabile in Python: **Sì**.

### `src/session/sensor_event_handler.hpp` (`nemo::SensorEventHandler`)

- `makePromise(tag)`: promise log.
  - Porting in Python: **No**.
- `onChannelOpenRequest(req)`: risponde SUCCESS.
  - Logica portabile in Python: **Sì** tramite hook `onSensorChannelOpenRequest` (bytes response) con fallback C++.
- `onSensorStartRequest(req)`: risponde SUCCESS; su DrivingStatus/NightMode invia anche batch (gate critico per video).
  - Logica portabile in Python: **Sì** tramite hook `onSensorStartRequest` che può ritornare `(resp_bytes, batch_bytes)`; se non fornito, resta fallback C++ (DrivingStatus UNRESTRICTED / NightMode).
- `sendDrivingStatusUnrestricted()`, `sendNightData(night)`: costruiscono e inviano `SensorBatch`.
  - Porting in Python: **Sì (logica)**, ma attualmente non c’è ponte Python per inviare `SensorBatch` su quel canale.
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Sensor", msg)` se presente.
  - Logica portabile in Python: **Sì**.

### `src/session/input_event_handler.hpp` (`nemo::InputEventHandler`)

- `makePromise(tag)`: promise log.
  - Porting in Python: **No**.
- `onChannelOpenRequest(req)`, `onKeyBindingRequest(req)`: rispondono SUCCESS e continuano `receive()`.
  - Logica portabile in Python: **Sì** tramite hook `onInputChannelOpenRequest` e `onKeyBindingRequest` (bytes response) con fallback C++.
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Input", msg)` se presente.
  - Logica portabile in Python: **Sì**.

### `src/session/navigation_event_handler.hpp` (`nemo::NavigationEventHandler`)

- `makePromise(tag)`: promise log.
  - Porting in Python: **No**.
- `onChannelOpenRequest(req)`: risponde SUCCESS.
  - Logica portabile in Python: **Sì** tramite hook `onNavigationChannelOpenRequest` (bytes response) con fallback C++.
- `onStatusUpdate`, `onTurnEvent`, `onDistanceEvent`: oggi sink silente (solo `receive()`).
  - Logica portabile in Python: **Sì**: ora forwardano a Python (`onNavigationStatusUpdate/TurnEvent/DistanceEvent`) passando `SerializeAsString()` (non-hot).
- `onChannelError(e)`: notifica Python via `IOrchestrator.onChannelError("Navigation", msg)` se presente.
  - Logica portabile in Python: **Sì**.

### `src/python/py_orchestrator.hpp` (`nemo::PyOrchestrator`)

Questa classe è un adapter C++ che implementa `IOrchestrator` e chiama metodi su un oggetto Python.  
È esattamente il pattern che descrivi (“creo l’oggetto in Python e passo la reference a un binding che la usa”).

- `PyOrchestrator(py::object)`, `~PyOrchestrator()`: gestisce lifetime e `py::gil_scoped_acquire`.
  - Porting in Python: **No** (serve per attraversare il confine C++/Python in modo sicuro).
- Metodi `IOrchestrator` (`setCryptor`, `onVersionStatus`, `onHandshake`, `getAuthCompleteResponse`, `onServiceDiscoveryRequest`, `onPingRequest`, `onAudioFocusRequest`, `onNavigationFocusRequest`, `onVoiceSessionRequest`, `onBatteryStatusNotification`, `onAvChannelSetupRequest`, `onChannelOpenRequest`, `onVideoFocusRequest`, `onVideoChannelOpenRequest`):
  - Logica portabile in Python: **Sì (target principale)**. In C++ rimane solo il forward + gestione GIL/None/errori.
- Helpers `callPythonMethod*`: centralizzano pattern di chiamata/gestione None/sink silente.
  - Porting in Python: **No** (sono parte dell’adapter).
- Fallback `handle_event(name, channel_id, payload)`: se un metodo specifico non esiste, l’adapter prova `handle_event(...)` (se presente) prima di lanciare (per i path “non-silent”).
  - Logica portabile in Python: **Sì** (riduce la superficie obbligatoria dell’orchestrator).
- Error hook `on_channel_error(source, message)`: riceve errori dai vari `onChannelError` C++.
  - Logica portabile in Python: **Sì**.

### `src/gst/gst_video_sink.hpp` (`nemo::GstVideoSink`)

- `GstVideoSink(width,height)`, `~GstVideoSink()`: setup e stop.
  - Porting in Python: **No (consigliato)**. Python può controllare GStreamer, ma qui è fondamentale evitare passaggio buffer in Python.
- `setWindowId(wid)`: salva WId (X11/Wayland).
  - Porting in Python: **Parziale** (ottenere WId è Python/UI; settarlo sul sink è C++).
- `startPipeline()`: crea `GstPipeline` e chiama `init()`, con gestione eccezioni.
  - Porting in Python: **Possibile** (via PyGObject), ma sconsigliato per uniformità e per gestione cross-thread col sink.
- `pushBuffer(ts,data,size)`: chiamato dal thread Asio; inoltra a pipeline.
  - Logica portabile in Python: **No (hot path)**.
- `stop()`, `isRunning()`, `width()`, `height()`: gestione stato.
  - Porting in Python: **Parziale** (API pubblica; l’implementazione resta C++).

### `src/gst/gst_pipeline.hpp` (`nemo::GstPipeline`)

- `init(window_handle,width,height)`: costruisce pipeline via `gst_parse_launch`, configura appsrc, overlay su WId, setta PLAYING, diagnostica bus error.
  - Porting in Python: **Possibile** (PyGObject), ma qui dipende da elementi e diagnostica low-level; tenerlo C++ evita overhead e mismatch di thread.
- `pushBuffer(ts_us,data,size)`: crea `GstBuffer` (memdup), setta PTS/DTS, push su appsrc.
  - Logica portabile in Python: **No (hot path)**.
- `stop()`: EOS, set state NULL, unref.
  - Porting in Python: **Possibile**, ma meglio co-locare con `init/pushBuffer` in C++.
- `isRunning()`: atomic flag.
  - Porting in Python: **N/A** (stato interno).
- `_readBusError()`: legge error/warn dal bus con timeout.
  - Porting in Python: **Possibile**, ma è diagnostica interna del pipeline.

---

## 4) Analisi implementazioni Python (`python/**`, escluso `python/aasdk_proto/**`)

### `python/app/crypto_manager.py`

Funzioni:
- `_find_file(paths)`: cerca e legge il primo file valido tra i path.
  - Porting in C++: **Sì**, ma non serve: è I/O semplice.
- `_get_pubkey_from_cert(cert_pem)`, `_get_pubkey_from_privkey(key_pem)`: invocano `openssl` CLI per estrarre pubkey.
  - Porting in C++: **Sì** (via OpenSSL API), ma qui è volutamente “zero dipendenze Python” e semplice da manutenere.
- `_validate_cert_key_pair(cert,key)`: confronta pubkey estratte.
  - Porting in C++: **Sì**, ma il valore aggiunto è minimo dato che `aasdk` verifica comunque.

Classe `CryptoManager`:
- `initialize()`: carica cert/key + valida coppia.
  - Porting in C++: **Sì**, ma tenere in Python accelera iterazione e riduce binding.
- `get_certificate()`, `get_private_key()`: accessors.
  - Porting in C++: **N/A** (dati già passati a C++).

### `python/test_interactive_phase4.py`

Classe `InteractiveOrchestrator` (metodi):
- `set_cryptor`, `_drain_tls_out`, `_step_handshake_and_collect`: pilotano handshake TLS usando `core.ICryptor`.
  - Porting in C++: **Possibile**, ma qui è proprio il punto: avere handshake/policy in Python (debuggabile, iterabile).
- `on_version_status`, `on_handshake`, `get_auth_complete_response`: completano TLS e inviano AuthComplete.
  - Porting in C++: **Sì** (aasdk lo fa), ma qui serve per controllo e sperimentazione.
- `on_service_discovery_request`: costruisce `ServiceDiscoveryResponse` con canali e capability.
  - Porting in C++: **Sì**, ma molto più comodo in Python (prototyping).
- `on_av_channel_setup_request`, `on_channel_open_request`, `on_video_focus_request`: risposte protocollari di gating.
  - Porting in C++: **Sì**, ma è logica “policy”; Python è appropriato.
- `on_ping_request`, `on_audio_focus_request`, `on_navigation_focus_request`, `on_voice_session_request`, `on_battery_status_notification`: risposte/handling control.
  - Porting in C++: **Sì**, ma Python va bene finché non diventa un path ad alta frequenza.

### `python/ui/main_window.py`

Classi:
- `PipelineBridge(QObject)`: signal Qt thread-safe cross-thread.
  - Porting in C++: **Sì** (Qt C++), ma l’app UI è Python/PyQt6: resta Python.
- `VideoWidget(QWidget)`: crea WId nativo e disabilita paint Qt.
  - Porting in C++: **Sì**, ma resta Python.
- `MainWindow(QMainWindow)`: wiring di `IoContextRunner`, `UsbHubManager`, `GstVideoSink`, orchestrator; gestisce lifecycle.
  - Porting in C++: **Sì** (Qt app nativa), ma non necessario: qui Python è la UI glue.

### Test / entrypoint vari

- `python/test_core.py`, `python/test.py`, `python/test_phase2.py`, `python/test_phase3.py`, `python/test_phase3_interactive.py`, `python/test_phase5_dump.py`, `python/test_headless.py`:
  - sono harness di test/integrazione e scaffolding.
  - Porting in C++: **Non consigliato**; restano Python per velocità e debug.

---

## 5) Riassunto (binding-first): cosa resta C++ come “guscio” e cosa diventa Python

**C++ (resta, ma idealmente thin):**
- plumbing verso librerie native (aasdk/libusb/asio/gstreamer) e ownership/teardown sicuro;
- implementazioni di interfacce callback C++ (EventHandler) che **forwardano** a Python;
- path hot: buffer video/audio (almeno fino a quando non si accetta overhead/copy/GIL).

**Python (diventa “il cervello”):**
- orchestrazione/policy: handshake, service discovery, focus/ping, gating canali (protobuf);
- configurazione feature (video dump, canali abilitati, logging levels, retry/backoff);
- UI e lifecycle (PyQt);
- certificati/tooling/test.

---

## 6) Candidati concreti per svuotare C++ (senza toccare hot path)

Se l’obiettivo è “massimizzare Python” mantenendo stabile il realtime:
- Spostare `UsbHubManager::ensureCertificatesExist()` in Python e rendere C++ “cert-path consumer” (o aggiungere una API binding per scrittura file).
- Rendere gli *EventHandler* C++ puri forwarder: parse minimo + `SerializeAsString()` + chiamata Python + invio risposta.
- Aggiungere un canale eventi C++→Python per dati non-hot (nav/sensor/stats/errori) con queue, evitando chiamate Python dentro callback ad alta frequenza.
- Tenere in C++ i buffer (video/audio) e far arrivare a Python solo metadati e comandi (start/stop/config).
