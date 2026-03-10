#pragma once

#include <iostream>
#include <aasdk/Channel/SensorSource/ISensorSourceServiceEventHandler.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include "iorchestrator.hpp"

namespace nemo
{

    class SensorEventHandler
        : public aasdk::channel::sensorsource::ISensorSourceServiceEventHandler,
          public std::enable_shared_from_this<SensorEventHandler>
    {
    public:
        using Pointer = std::shared_ptr<SensorEventHandler>;

        explicit SensorEventHandler(boost::asio::io_service::strand &strand,
                                    aasdk::channel::sensorsource::ISensorSourceService::Pointer channel,
                                    std::shared_ptr<IOrchestrator> orchestrator)
            : strand_(strand),
              channel_(std::move(channel)),
              orchestrator_(std::move(orchestrator)) {}

        aasdk::channel::SendPromise::Pointer makePromise(const char *tag)
        {
            auto p = aasdk::channel::SendPromise::defer(strand_);
            p->then(
                []() {},
                [tag](const aasdk::error::Error &e)
                {
                    std::cerr << "[" << tag << "] send FAILED: " << e.what() << std::endl;
                });
            return p;
        }

        // -----------------------------------------------------------------------
        // ChannelOpenRequest -> ChannelOpenResponse(SUCCESS)
        // Ref: SensorService.cpp::onChannelOpenRequest()
        // -----------------------------------------------------------------------
        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            std::cout << "[Sensor] ChannelOpenRequest received." << std::endl;

            aap_protobuf::service::control::message::ChannelOpenResponse response;
            response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
            channel_->sendChannelOpenResponse(response, makePromise("Sensor/ChannelOpenResponse"));
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // SensorStartRequest -> SensorStartResponse(SUCCESS)
        //   + promise->then per SENSOR_DRIVING_STATUS_DATA: sendDrivingStatusUnrestricted()
        //   + promise->then per SENSOR_NIGHT_MODE:          sendNightData(false)
        // Ref: SensorService.cpp::onSensorStartRequest()
        // GATE CRITICO: senza DrivingStatus UNRESTRICTED Android non avvia H.264.
        // -----------------------------------------------------------------------
        void onSensorStartRequest(
            const aap_protobuf::service::sensorsource::message::SensorRequest &request) override
        {
            std::cout << "[Sensor] SensorStartRequest type=" << request.type() << std::endl;

            aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
            response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            auto self = this->shared_from_this();

            if (request.type() ==
                aap_protobuf::service::sensorsource::message::SensorType::SENSOR_DRIVING_STATUS_DATA)
            {
                // Ref: SensorService.cpp: promise->then(sendDrivingStatusUnrestricted)
                promise->then(
                    [self]()
                    { self->sendDrivingStatusUnrestricted(); },
                    [](const aasdk::error::Error &e)
                    {
                        std::cerr << "[Sensor] SensorStartResponse(DRIVING) FAILED: " << e.what() << std::endl;
                    });
            }
            else if (request.type() ==
                     aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE)
            {
                // Ref: SensorService.cpp: promise->then(sendNightData)
                promise->then(
                    [self]()
                    { self->sendNightData(false); },
                    [](const aasdk::error::Error &e)
                    {
                        std::cerr << "[Sensor] SensorStartResponse(NIGHT) FAILED: " << e.what() << std::endl;
                    });
            }
            else
            {
                promise->then(
                    []() {},
                    [](const aasdk::error::Error &e)
                    {
                        std::cerr << "[Sensor] SensorStartResponse FAILED: " << e.what() << std::endl;
                    });
            }

            channel_->sendSensorStartResponse(response, std::move(promise));
            channel_->receive(this->shared_from_this());
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            std::cerr << "[Sensor] Channel Error: " << e.what() << std::endl;
        }

    private:
        // -----------------------------------------------------------------------
        // sendDrivingStatusUnrestricted
        // Ref: SensorService.cpp::sendDrivingStatusUnrestricted()
        // Invia DRIVE_STATUS_UNRESTRICTED: sblocca lo streaming video su Android.
        // -----------------------------------------------------------------------
        void sendDrivingStatusUnrestricted()
        {
            std::cout << "[Sensor] sendDrivingStatusUnrestricted() -> UNRESTRICTED" << std::endl;

            aap_protobuf::service::sensorsource::message::SensorBatch indication;
            indication.add_driving_status_data()->set_status(
                aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then(
                []() {},
                [](const aasdk::error::Error &e)
                {
                    std::cerr << "[Sensor] sendDrivingStatusUnrestricted FAILED: " << e.what() << std::endl;
                });
            channel_->sendSensorEventIndication(indication, std::move(promise));
        }

        // -----------------------------------------------------------------------
        // sendNightData
        // Ref: SensorService.cpp::sendNightData()
        // Invia NightMode: false = giorno, true = notte.
        // -----------------------------------------------------------------------
        void sendNightData(bool night_mode)
        {
            std::cout << "[Sensor] sendNightData() night_mode=" << night_mode << std::endl;

            aap_protobuf::service::sensorsource::message::SensorBatch indication;
            indication.add_night_mode_data()->set_night_mode(night_mode);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then(
                []() {},
                [](const aasdk::error::Error &e)
                {
                    std::cerr << "[Sensor] sendNightData FAILED: " << e.what() << std::endl;
                });
            channel_->sendSensorEventIndication(indication, std::move(promise));
        }

        boost::asio::io_service::strand &strand_;
        aasdk::channel::sensorsource::ISensorSourceService::Pointer channel_;
        std::shared_ptr<IOrchestrator> orchestrator_;
    };

} // namespace nemo
