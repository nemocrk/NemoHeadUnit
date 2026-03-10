#pragma once

#include <string>
#include <memory>
#include <aasdk/Messenger/ICryptor.hpp>

namespace nemo
{

    class IOrchestrator
    {
    public:
        virtual ~IOrchestrator() = default;

        virtual void setCryptor(std::shared_ptr<aasdk::messenger::ICryptor> cryptor) = 0;

        // ---------------------------------------------------------------------------
        // Handshake TLS
        // ---------------------------------------------------------------------------

        // Innesca il primo chunk dell'handshake SSL
        virtual std::string onVersionStatus(int major, int minor, int status) = 0;

        // Riceve chunk TLS e ritorna il prossimo; "" = handshake completato
        virtual std::string onHandshake(const std::string &payload_bytes) = 0;

        // Dopo onHandshake che ritorna "": C++ chiede i byte AuthComplete
        virtual std::string getAuthCompleteResponse() = 0;

        // ---------------------------------------------------------------------------
        // Control Channel — messaggi service-level
        // Ref: AndroidAutoEntity-17.cpp
        // ---------------------------------------------------------------------------

        // ServiceDiscoveryRequest -> ServiceDiscoveryResponse
        virtual std::string onServiceDiscoveryRequest(const std::string &request_bytes) = 0;

        // PingRequest -> PingResponse
        virtual std::string onPingRequest(const std::string &request_bytes) = 0;

        // AudioFocusRequest -> AudioFocusNotification (GAIN|LOSS)
        virtual std::string onAudioFocusRequest(const std::string &request_bytes) = 0;

        // NavigationFocusRequestNotification -> NavFocusNotification(NAV_FOCUS_PROJECTED)
        // Ref: AndroidAutoEntity-17.cpp::onNavigationFocusRequest()
        virtual std::string onNavigationFocusRequest(const std::string &request_bytes) = 0;

        // VoiceSessionNotification — sink silente, nessuna risposta attesa
        // Ref: AndroidAutoEntity-17.cpp::onVoiceSessionRequest()
        virtual std::string onVoiceSessionRequest(const std::string &request_bytes) = 0;

        // BatteryStatusNotification — sink silente, nessuna risposta attesa
        // Ref: AndroidAutoEntity-17.cpp::onBatteryStatusNotification()
        virtual std::string onBatteryStatusNotification(const std::string &request_bytes) = 0;

        // ---------------------------------------------------------------------------
        // Media Channels — handshake a 3 step (Setup -> Open -> FocusRequest)
        // Ref: AudioMediaSinkService-19.cpp, VideoMediaSinkService-14.cpp
        // ---------------------------------------------------------------------------

        // Step 1: AVChannelSetupRequest -> AVChannelSetupResponse (= Config proto)
        //   status=STATUS_READY, max_unacked=1, configuration_indices=[0]
        //   Per CH_VIDEO (3): il ritorno contiene ANCHE VideoFocusIndication concatenata
        //   channel_id: uno di {3=VIDEO, 4=MEDIA_AUDIO, 5=SPEECH_AUDIO, 6=SYSTEM_AUDIO, 9=MIC}
        virtual std::string onAvChannelSetupRequest(aasdk::messenger::ChannelId channel_id, const std::string &request_bytes) = 0;

        // Step 2: ChannelOpenRequest -> ChannelOpenResponse(STATUS_SUCCESS)
        //   Per CH_VIDEO (3): il ritorno contiene ANCHE VideoFocusIndication concatenata
        //   channel_id: qualsiasi canale che ha superato lo step 1
        virtual std::string onChannelOpenRequest(aasdk::messenger::ChannelId channel_id, const std::string &request_bytes) = 0;

        // Step 3 (solo CH_VIDEO): VideoFocusRequestNotification -> VideoFocusIndication(PROJECTED)
        //   Questo e' il gate finale che sblocca lo stream H.264
        //   Ref: VideoMediaSinkService-14.cpp::onVideoFocusRequest()
        virtual std::string onVideoFocusRequest(const std::string &request_bytes) = 0;

        // ---------------------------------------------------------------------------
        // Video stream (Phase 5)
        // Ref: VideoMediaSinkService-14.cpp::onChannelOpenRequest() post-aperto
        // ---------------------------------------------------------------------------

        // ChannelOpenRequest specifico del canale video (chiamato DOPO onChannelOpenRequest)
        // In Phase 5 qui si inizializza il sink GStreamer/libavcodec
        virtual std::string onVideoChannelOpenRequest(const std::string &request_bytes) = 0;
    };

} // namespace nemo
