# Channel Implementation Gaps (POC)

Questo documento riepiloga, per ogni canale AASDK gestito in POC:
1) metodi gestiti e non gestiti nella logica Python (una response `b""` o nessuna azione = non gestito)
2) dove trovare un esempio o una base per implementare i metodi non gestiti
3) message id non gestiti da AASDK (log "Message Id not Handled")

Nota: in AASDK molti message id “non gestiti” sono messaggi **outbound** (inviati dall’HU) o non previsti per quel lato. In quel caso AASDK non li gestisce perché non arrivano dal telefono.

**Control (ChannelId.CONTROL)**

Python: metodi gestiti
- `on_version_response` (handshake)
- `on_handshake` (TLS + auth complete)
- `on_service_discovery_request` (ServiceDiscoveryResponse)
- `on_audio_focus_request` (AudioFocusNotification)
- `on_navigation_focus_request` (NavFocusNotification)
- `on_ping_request` (PingResponse)
- `on_bye_bye_request` (ByeByeResponse)

Python: metodi non gestiti
- `on_voice_session_request` (ritorna `b""`)
- `on_battery_status_notification` (ritorna `b""`)
- `on_ping_response` (no-op)
- `on_bye_bye_response` (no-op)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/AndroidAutoEntity.cpp` contiene gestione di AudioFocus, NavFocus, VoiceSession, BatteryStatus.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MESSAGE_VERSION_REQUEST`
- `MESSAGE_AUTH_COMPLETE`
- `MESSAGE_SERVICE_DISCOVERY_RESPONSE`
- `MESSAGE_CHANNEL_OPEN_REQUEST`
- `MESSAGE_CHANNEL_OPEN_RESPONSE`
- `MESSAGE_CHANNEL_CLOSE_NOTIFICATION`
- `MESSAGE_NAV_FOCUS_NOTIFICATION`
- `MESSAGE_AUDIO_FOCUS_NOTIFICATION`
- `MESSAGE_CAR_CONNECTED_DEVICES_REQUEST`
- `MESSAGE_CAR_CONNECTED_DEVICES_RESPONSE`
- `MESSAGE_USER_SWITCH_REQUEST`
- `MESSAGE_CALL_AVAILABILITY_STATUS`
- `MESSAGE_USER_SWITCH_RESPONSE`
- `MESSAGE_SERVICE_DISCOVERY_UPDATE`
- `MESSAGE_UNEXPECTED_MESSAGE`
- `MESSAGE_FRAMING_ERROR`

**Audio Media Sink (ChannelId.MEDIA_SINK_* AUDIO)**

Python: metodi gestiti
- `on_media_channel_setup_request` (Config STATUS_READY)
- `on_channel_open_request` (ChannelOpenResponse)

Python: metodi non gestiti
- `on_media_channel_start` (ritorna `b""`)
- `on_media_channel_stop` (ritorna `b""`)
- `on_media_with_timestamp` (ritorna `b""`, nessun Ack)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/AudioMediaSinkService.cpp` implementa start/stop e Ack.
- `external sources/MediaAudioService.cpp`, `GuidanceAudioService.cpp`, `SystemAudioService.cpp` mostrano pattern per i 3 stream.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MEDIA_MESSAGE_CONFIG`
- `MEDIA_MESSAGE_ACK`
- `MEDIA_MESSAGE_MICROPHONE_REQUEST`
- `MEDIA_MESSAGE_MICROPHONE_RESPONSE`
- `MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST`
- `MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REQUEST`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REPLY`
- `MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION`

**Video Media Sink (ChannelId.MEDIA_SINK_VIDEO)**

Python: metodi gestiti
- `on_media_channel_setup_request`
- `on_channel_open_request`
- `on_video_focus_request` (VideoFocusNotification)

Python: metodi non gestiti
- `on_media_channel_start` (ritorna `b""`)
- `on_media_channel_stop` (ritorna `b""`)
- `on_media_with_timestamp` (ritorna `b""`, nessun Ack)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/VideoMediaSinkService.cpp` e `VideoService.cpp` per focus, start/stop e ack.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MEDIA_MESSAGE_CONFIG`
- `MEDIA_MESSAGE_ACK`
- `MEDIA_MESSAGE_MICROPHONE_REQUEST`
- `MEDIA_MESSAGE_MICROPHONE_RESPONSE`
- `MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REQUEST`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REPLY`
- `MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION`

**Sensor Source (ChannelId.SENSOR)**

Python: metodi gestiti
- `on_channel_open_request`
- `on_sensor_start_request` (SensorStartResponse + opzionale SensorBatch)

Python: metodi non gestiti
- `on_channel_error` (no-op)
- invio periodico di batch (oltre il primo batch iniziale)

Esempi/linee guida
- `external sources/SensorService.cpp` per start/batch e gestione eventi.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `SENSOR_MESSAGE_RESPONSE`
- `SENSOR_MESSAGE_BATCH`
- `SENSOR_MESSAGE_ERROR`

**Input Source (ChannelId.INPUT_SOURCE)**

Python: metodi gestiti
- `on_channel_open_request`
- `on_key_binding_request` (KeyBindingResponse)

Python: metodi non gestiti
- `on_key_binding_request`: `InputReport` (ritorna `b""`)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/InputSourceService.cpp` per key binding + report.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `INPUT_MESSAGE_INPUT_REPORT`
- `INPUT_MESSAGE_KEY_BINDING_RESPONSE`
- `INPUT_MESSAGE_INPUT_FEEDBACK`

**Navigation Status (ChannelId.NAVIGATION_STATUS)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_status_update` (no-op)
- `on_turn_event` (no-op)
- `on_distance_event` (no-op)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/NavigationStatusService.cpp` per status/turn/distance.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `INSTRUMENT_CLUSTER_START`
- `INSTRUMENT_CLUSTER_STOP`
- `INSTRUMENT_CLUSTER_NAVIGATION_STATE`
- `INSTRUMENT_CLUSTER_NAVIGATION_CURRENT_POSITION`

**Bluetooth (ChannelId.BLUETOOTH)**

Python: metodi gestiti
- `on_channel_open_request`
- `on_bluetooth_pairing_request` (PairingResponse + AuthenticationData)

Python: metodi non gestiti
- `on_bluetooth_authentication_result` (no-op)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/BluetoothService.cpp` per pairing/auth.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `BLUETOOTH_MESSAGE_PAIRING_RESPONSE`
- `BLUETOOTH_MESSAGE_AUTHENTICATION_DATA`

**Generic Notification (ChannelId.GENERIC_NOTIFICATION)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_channel_error` (no-op)
- nessuna gestione di subscribe/unsubscribe/message/ack

Esempi/linee guida
- `external sources/GenericNotificationService.cpp` (stub, non gestisce messaggi)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `GENERIC_NOTIFICATION_SUBSCRIBE`
- `GENERIC_NOTIFICATION_UNSUBSCRIBE`
- `GENERIC_NOTIFICATION_MESSAGE`
- `GENERIC_NOTIFICATION_ACK`

**Media Browser (ChannelId.MEDIA_BROWSER)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_channel_error` (no-op)
- nessuna gestione dei messaggi media browser

Esempi/linee guida
- `external sources/MediaBrowserService.cpp` (stub, non gestisce messaggi)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MEDIA_ROOT_NODE`
- `MEDIA_SOURCE_NODE`
- `MEDIA_LIST_NODE`
- `MEDIA_SONG_NODE`
- `MEDIA_GET_NODE`
- `MEDIA_BROWSE_INPUT`

**Media Playback Status (ChannelId.MEDIA_PLAYBACK_STATUS)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_metadata_update` (parsing senza azione)
- `on_playback_update` (parsing senza azione)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/MediaPlaybackStatusService.cpp` (stub, solo open)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MEDIA_PLAYBACK_INPUT`

**Media Source (ChannelId.MEDIA_SOURCE_MICROPHONE)**

Python: metodi gestiti
- `on_channel_open_request`
- `on_media_channel_setup_request`
- `on_media_source_open_request` (MicrophoneResponse)

Python: metodi non gestiti
- `on_media_channel_ack_indication` (no-op)
- invio stream audio (nessuna chiamata a `send_media_source_with_timestamp`)
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/MediaSourceService.cpp` e `MicrophoneMediaSourceService.cpp` per apertura, ack e streaming.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `MEDIA_MESSAGE_DATA`
- `MEDIA_MESSAGE_CODEC_CONFIG`
- `MEDIA_MESSAGE_START`
- `MEDIA_MESSAGE_STOP`
- `MEDIA_MESSAGE_CONFIG`
- `MEDIA_MESSAGE_MICROPHONE_RESPONSE`
- `MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST`
- `MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REQUEST`
- `MEDIA_MESSAGE_UPDATE_UI_CONFIG_REPLY`
- `MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION`

**Phone Status (ChannelId.PHONE_STATUS)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_channel_error` (no-op)
- nessuna gestione di `PHONE_STATUS` e `PHONE_STATUS_INPUT`

Esempi/linee guida
- `external sources/PhoneStatusService.cpp` (stub, non gestisce messaggi)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `PHONE_STATUS`
- `PHONE_STATUS_INPUT`

**Radio (ChannelId.RADIO)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_channel_error` (no-op)
- nessuna gestione dei messaggi radio

Esempi/linee guida
- `external sources/RadioService.cpp` (stub, non gestisce messaggi)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `RADIO_MESSAGE_ACTIVE_RADIO_NOTIFICATION`
- `RADIO_MESSAGE_SELECT_ACTIVE_RADIO_REQUEST`
- `RADIO_MESSAGE_STEP_CHANNEL_REQUEST`
- `RADIO_MESSAGE_STEP_CHANNEL_RESPONSE`
- `RADIO_MESSAGE_SEEK_STATION_REQUEST`
- `RADIO_MESSAGE_SEEK_STATION_RESPONSE`
- `RADIO_MESSAGE_SCAN_STATIONS_REQUEST`
- `RADIO_MESSAGE_SCAN_STATIONS_RESPONSE`
- `RADIO_MESSAGE_TUNE_TO_STATION_REQUEST`
- `RADIO_MESSAGE_TUNE_TO_STATION_RESPONSE`
- `RADIO_MESSAGE_GET_PROGRAM_LIST_REQUEST`
- `RADIO_MESSAGE_GET_PROGRAM_LIST_RESPONSE`
- `RADIO_MESSAGE_STATION_PRESETS_NOTIFICATION`
- `RADIO_MESSAGE_CANCEL_OPERATIONS_REQUEST`
- `RADIO_MESSAGE_CANCEL_OPERATIONS_RESPONSE`
- `RADIO_MESSAGE_CONFIGURE_CHANNEL_SPACING_REQUEST`
- `RADIO_MESSAGE_CONFIGURE_CHANNEL_SPACING_RESPONSE`
- `RADIO_MESSAGE_RADIO_STATION_INFO_NOTIFICATION`
- `RADIO_MESSAGE_MUTE_RADIO_REQUEST`
- `RADIO_MESSAGE_MUTE_RADIO_RESPONSE`
- `RADIO_MESSAGE_GET_TRAFFIC_UPDATE_REQUEST`
- `RADIO_MESSAGE_GET_TRAFFIC_UPDATE_RESPONSE`
- `RADIO_MESSAGE_RADIO_SOURCE_REQUEST`
- `RADIO_MESSAGE_RADIO_SOURCE_RESPONSE`
- `RADIO_MESSAGE_STATE_NOTIFICATION`

**Vendor Extension (ChannelId.VENDOR_EXTENSION)**

Python: metodi gestiti
- `on_channel_open_request`

Python: metodi non gestiti
- `on_channel_error` (no-op)
- nessuna gestione di messaggi vendor-specific

Esempi/linee guida
- `external sources/VendorExtensionService.cpp` (stub, non gestisce messaggi)

AASDK: MessageId non gestiti (log "Message Id not Handled")
- Tutti i message id vendor-specific (AASDK non implementa un enum dedicato in questo canale).

**Wifi Projection (ChannelId.WIFI_PROJECTION)**

Python: metodi gestiti
- `on_channel_open_request`
- `on_wifi_credentials_request` (WifiCredentialsResponse)

Python: metodi non gestiti
- `on_channel_error` (no-op)

Esempi/linee guida
- `external sources/WifiProjectionService.cpp` per credenziali Wi-Fi.

AASDK: MessageId non gestiti (log "Message Id not Handled")
- `WIFI_MESSAGE_CREDENTIALS_RESPONSE`
