# Guida all'Implementazione di aasdk

Questa guida descrive come utilizzare `aasdk` per costruire l'emulatore Android Auto in NemoHeadUnit.

## Principali Metodi da Implementare
Per creare una HeadUnit funzionante con `aasdk`, è necessario implementare le interfacce per la gestione del ciclo di vita della connessione e i vari canali (channels):
1. **Inizializzazione:** Configurazione delle librerie `Boost.Asio` per i task asincroni, inizializzazione di `libusb` per il trasporto dati e di `OpenSSL` per i certificati/criptazione.
2. **Channel Services:** Ogni canale di comunicazione deve essere gestito. I principali sono:
   - `IVideoService`: per decodificare il flusso video H.264 inviato dal telefono.
   - `IAudioInputService` / `IAudioOutputService`: per gestire il microfono (comandi vocali) e la riproduzione audio (Media audio e System/Speech audio).
   - `IInputService` / `ITouchService`: per inviare i comandi di tocco o i controlli rotary dalla UI al dispositivo.
3. **Promise/Handler:** Implementare i metodi asincroni (`aasdk::messenger::IMessenger`, `aasdk::transport::ITransport`) per il parsing dei pacchetti Protobuf scambiati tra telefono e HeadUnit.

## Flusso di Connessione Cablato (USB)
Il flusso cablato si basa sul protocollo **AOAP (Android Open Accessory Protocol)**:
1. **Rilevamento USB:** Ascolto degli eventi USB hotplug. Quando un dispositivo Android viene collegato, l'emulatore invia le stringhe di identificazione AOAP.
2. **Switch in Modalità Accessorio:** Il dispositivo Android riconosce l'emulatore e riavvia la connessione USB in "Accessory Mode".
3. **Handshake e Autenticazione:** Avvio della connessione SSL. Lo scambio di certificati garantisce che la comunicazione sia cifrata.
4. **Channel Setup:** Android Auto richiede l'apertura dei canali (Video, Audio, Input). La HeadUnit conferma quali canali supporta.
5. **Streaming Dati:** Inizia lo streaming video e la ricezione dell'input.

## Flusso di Connessione Wireless (TCP/IP)
Per il wireless, il trasporto avviene tramite socket TCP (e non tramite USB):
1. **Scambio di Rete:** Solitamente la HeadUnit avvia un Access Point Wi-Fi e si aspetta che il telefono si connetta. La negoziazione delle credenziali di rete avviene spesso via Bluetooth preventivo o tramite una prima connessione USB.
2. **Connessione TCP:** Una volta che il telefono è nella stessa rete, tenta di stabilire una connessione TCP verso la HeadUnit (sulla porta configurata, es. 5277, come avviene in ADB Tunneling o connessioni wireless standard).
3. **Handshake SSL su TCP:** Simile al flusso USB, si instaura la connessione sicura scambiando i certificati SSL sul socket TCP.
4. **Avvio Canali:** Vengono aperti gli stessi canali logici (Audio, Video, Touch, ecc.) instradati però sui pacchetti TCP.