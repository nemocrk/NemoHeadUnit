# NemoHeadUnit

NemoHeadUnit è un emulatore open-source di HeadUnit per Android Auto, costruito da zero utilizzando la libreria `aasdk`. Questo progetto mira a fornire un'interfaccia moderna e reattiva per interfacciarsi con i dispositivi Android tramite Android Auto (cablato e wireless).

## Scopo del Progetto
L'obiettivo è creare una piattaforma flessibile ed estensibile per emulare l'infotainment di un'automobile. Potrà essere eseguito su vari hardware (es. Raspberry Pi, PC Linux/Windows) fornendo un supporto completo per la proiezione video, la gestione dell'audio e gli input touchscreen/rotary.

## Stack Tecnologico
- **Core Library:** `aasdk` (C++, Boost, Protocol Buffers, OpenSSL, libusb)
- **Linguaggio (Core):** C++ per l'interazione diretta con aasdk.
- **Interfaccia Grafica:** Python (tramite binding o wrapper) o framework UI moderni (da definire in base agli sviluppi).
- **Comunicazione USB/TCP:** AOAP (Android Open Accessory Protocol), TCP/IP per la connessione wireless.

## Qualità del Codice e Test
Per garantire l'affidabilità del sistema:
- **Unit Testing:** Copertura minima del 70% per i moduli core (es. parsing pacchetti, gestione stato connessione).
- **Integrazione:** Test automatizzati per il flusso di handshake USB e TCP.
- **Code Coverage:** Richiesto l'uso di tool di coverage nel CI/CD per assicurare che le nuove PR mantengano o aumentino il livello minimo di test.