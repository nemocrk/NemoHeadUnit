# POC Event Handler — Guida di Implementazione (strict)

Questa guida descrive **passo‑passo** come aggiungere un nuovo event handler “strict” (C++ → Python) senza logica C++.
L’obiettivo è: C++ fa solo pass‑through, Python esegue tutta la logica.

## 1) Crea il file handler C++ (header‑only)

**File:** `POC/<nome>_event_handler.hpp`

Requisiti obbligatori:
- La classe deve implementare l’interfaccia AASDK del handler.
- Ogni metodo deve chiamare **solo** `CALL_PY(...)`.
- Esporre `channel`, `strand` (getter) per uso Python.
- Ricevere `EventBinding` nel costruttore e **validare non‑null**.

Schema minimo:
```cpp
#pragma once
#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>
#include <aasdk/.../I<HandlerInterface>.hpp>
#include "<channel_interface>.hpp"
#include "POC/handlers/common/event_binding.hpp"
#include "POC/handlers/common/event_handler_utils.hpp"

class <Handler>
  : public <AASDKInterface>,
    public EventHandlerBase<<ChannelPtr>>,
    public std::enable_shared_from_this<Handler>
{
public:
  <Handler>(boost::asio::io_service::strand& strand,
            <ChannelPtr> channel,
            std::shared_ptr<poc::EventBinding> binding)
    : EventHandlerBase(strand, std::move(channel)), binding_(std::move(binding))
  { if (!binding_) throw std::runtime_error("<Handler>: null binding"); }

  using EventHandlerBase::channel;
  using EventHandlerBase::strand;

  // Per ogni metodo dell’interfaccia:
  void onXxx(const Proto& msg) override { CALL_PY(msg); }
  void onYyy(...) override { CALL_PY(""); }

private:
  std::shared_ptr<poc::EventBinding> binding_;
};
```

## 2) Binding C++ per canale + handler

**Dove:** nello stesso header del handler (`POC/<nome>_event_handler.hpp`).

Requisiti obbligatori:
- Esporre il canale con i metodi `send_*` e `receive`.
- Usare `require_typed<T>(ProtobufMessage&)` per validare i protobuf.
- Per i `send_*` usare `with_promise(strand, fn, then_cb)` così la promise resta nascosta e puoi agganciare un callback Python opzionale.
- Esporre `AudioEventHandler` (o equivalente) con `channel`, `strand`.

Esempio minimo (già in `POC/audio_event_handler.hpp`):
```cpp
inline void init_<handler>_binding(pybind11::module_& m) {
  pybind11::class_<ChannelType, std::shared_ptr<ChannelType>>(m, "Channel")
    .def("get_id", [](ChannelType& ch){ return static_cast<int>(ch.getId()); })
    .def("receive", ...)
    .def("send_*", [](ChannelType& ch, const poc::ProtobufMessage& msg, boost::asio::io_service::strand& strand, const std::string& tag, pybind11::object then_cb) {
        with_promise(strand, [&](auto p){
            ch.send*(require_typed<ExpectedProto>(msg), std::move(p));
        }, then_cb);
    }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

  pybind11::class_<Handler, std::shared_ptr<Handler>>(m, "Handler")
    .def_property_readonly("channel", &Handler::channel)
    .def_property_readonly("strand", &Handler::strand);
}
```

## 3) Binding Python “event” (una sola volta)

**File già esistente:** `POC/handlers/common/event_binding.hpp`

Non va toccato per ogni handler.  
Include: `EventBinding` + `Strand.dispatch`.

## 4) Python logic file

**File:** `python/logic/<nome>_event_handler.py`

Regole:
- I nomi dei metodi sono in **snake_case** (auto‑mapping da `__func__`).
- Firma sempre: `def metodo(self, handler, payload):`
- `payload` è bytes (protobuf serializzato o stringa).
- Usa `handler.channel`, `handler.strand`.
- Per `channel_id` usa `handler.channel.get_id()`.
- Per protobuf: usa `aasdk_proto` per costruire i bytes e poi:
  `msg = protobuf.GetProtobuf("pkg.Message")` + `parse_from_string`.

Esempio:
```python
class <HandlerLogic>:
    def on_media_channel_setup_request(self, handler, payload):
        ch = handler.channel
        strand = handler.strand
        channel_id = handler.channel.get_id()
        # costruisci bytes con aasdk_proto, poi:
        msg = protobuf.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
        msg.parse_from_string(payload)
        ch.send_channel_setup_response(msg, strand, "Tag")
        ch.receive(handler)
```

Nota: i `send_*` accettano `then_cb=None` opzionale. Se passato, viene chiamato con `None` su successo oppure con una stringa di errore su failure.

## 5) CMake: wrapper del modulo

**File:** `CMakeLists.txt`

Per ogni handler aggiungi:
```cmake
create_pybind_module(<module_name> init_<handler>_binding
    HEADER POC/<nome>_event_handler.hpp
)
target_include_directories(<module_name> PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/POC
    ${aasdk_SOURCE_DIR}/include
)
target_link_libraries(<module_name> PRIVATE
    aasdk Boost::system protobuf::libprotobuf Threads::Threads
)
```

## 6) Naming e mapping (obbligatorio)

- C++ `onMediaChannelSetupRequest` → Python `on_media_channel_setup_request`
- La conversione è gestita da `EventBinding::camel_to_snake`.
- Se cambi il nome del metodo C++ devi cambiare il metodo Python.

## 7) Checklist finale (prima di compilare)

- [ ] Handler C++ header‑only creato
- [ ] `CALL_PY(...)` usato in **tutti** i metodi
- [ ] Binding canale + handler definito nello header
- [ ] Modulo CMake creato con `create_pybind_module`
- [ ] File Python con metodi snake_case
- [ ] `python` usa `protobuf.GetProtobuf(...)`
- [ ] `channel_id` letto via `channel.get_id()` (non salvato nel handler)

Questa guida è “strict”: **nessuna** logica applicativa rimane in C++. Se serve performance (es. `onMediaWithTimestampIndication`), è l’unica eccezione ammessa e va motivata esplicitamente.
