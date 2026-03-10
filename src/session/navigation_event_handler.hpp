#pragma once

#include <iostream>
#include <aasdk/Channel/NavigationStatus/INavigationStatusServiceEventHandler.hpp>
#include <aasdk/Channel/NavigationStatus/NavigationStatusService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include "iorchestrator.hpp"

namespace nemo {

// ---------------------------------------------------------------------------
// NavigationEventHandler
// Gestisce CH=0x0c (NAVIGATION_STATUS).
// Protocollo:
//   1. Android apre il canale -> onChannelOpenRequest -> ChannelOpenResponse(OK)
//   2. Android invia aggiornamenti nav (status/turn/distance) -> sink silente
//      In Phase 5 si potrà esporre questi dati a Python via IOrchestrator.
// Ref: NavigationStatusService.cpp (openauto)
// ---------------------------------------------------------------------------
class NavigationEventHandler
    : public aasdk::channel::navigationstatus::INavigationStatusServiceEventHandler,
      public std::enable_shared_from_this<NavigationEventHandler>
{
public:
    using Pointer = std::shared_ptr<NavigationEventHandler>;

    explicit NavigationEventHandler(
        boost::asio::io_service::strand &strand,
        aasdk::channel::navigationstatus::INavigationStatusService::Pointer channel,
        std::shared_ptr<IOrchestrator> orchestrator)
        : strand_(strand),
          channel_(std::move(channel)),
          orchestrator_(std::move(orchestrator)) {}

    aasdk::channel::SendPromise::Pointer makePromise(const char *tag)
    {
        auto p = aasdk::channel::SendPromise::defer(strand_);
        p->then(
            []() {},
            [tag](const aasdk::error::Error &e) {
                std::cerr << "[" << tag << "] send FAILED: " << e.what() << std::endl;
            });
        return p;
    }

    // -----------------------------------------------------------------------
    // ChannelOpenRequest -> ChannelOpenResponse(SUCCESS)
    // Ref: NavigationStatusService.cpp::onChannelOpenRequest()
    // -----------------------------------------------------------------------
    void onChannelOpenRequest(
        const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
    {
        std::cout << "[Navigation] ChannelOpenRequest received." << std::endl;

        aap_protobuf::service::control::message::ChannelOpenResponse response;
        response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
        channel_->sendChannelOpenResponse(response, makePromise("Navigation/ChannelOpenResponse"));
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Sink silente: ricevi e continua il loop.
    // Ref: NavigationStatusService.cpp::onStatusUpdate / onTurnEvent / onDistanceEvent
    // -----------------------------------------------------------------------
    void onStatusUpdate(
        const aap_protobuf::service::navigationstatus::message::NavigationStatus &navStatus) override
    {
        channel_->receive(this->shared_from_this());
    }

    void onTurnEvent(
        const aap_protobuf::service::navigationstatus::message::NavigationNextTurnEvent &turnEvent) override
    {
        channel_->receive(this->shared_from_this());
    }

    void onDistanceEvent(
        const aap_protobuf::service::navigationstatus::message::NavigationNextTurnDistanceEvent &distanceEvent) override
    {
        channel_->receive(this->shared_from_this());
    }

    void onChannelError(const aasdk::error::Error &e) override
    {
        std::cerr << "[Navigation] Channel Error: " << e.what() << std::endl;
    }

private:
    boost::asio::io_service::strand &strand_;
    aasdk::channel::navigationstatus::INavigationStatusService::Pointer channel_;
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
