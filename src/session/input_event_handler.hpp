#pragma once

#include <iostream>
#include <aasdk/Channel/InputSource/IInputSourceServiceEventHandler.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include "iorchestrator.hpp"

namespace nemo {

// ---------------------------------------------------------------------------
// InputEventHandler
// Gestisce CH=0x08 (INPUT_SOURCE).
// Protocollo:
//   1. Android apre il canale  -> onChannelOpenRequest  -> ChannelOpenResponse(OK)
//   2. Android invia KeyBinding -> onKeyBindingRequest   -> KeyBindingResponse(OK)
//      Da quel momento Android accetta gli InputReport che noi inviamo.
// Ref: InputSourceService.cpp (openauto)
// ---------------------------------------------------------------------------
class InputEventHandler
    : public aasdk::channel::inputsource::IInputSourceServiceEventHandler,
      public std::enable_shared_from_this<InputEventHandler>
{
public:
    using Pointer = std::shared_ptr<InputEventHandler>;

    explicit InputEventHandler(boost::asio::io_service::strand &strand,
                               aasdk::channel::inputsource::IInputSourceService::Pointer channel,
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
    // Ref: InputSourceService.cpp::onChannelOpenRequest()
    // -----------------------------------------------------------------------
    void onChannelOpenRequest(
        const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
    {
        std::cout << "[Input] ChannelOpenRequest received." << std::endl;

        aap_protobuf::service::control::message::ChannelOpenResponse response;
        response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
        channel_->sendChannelOpenResponse(response, makePromise("Input/ChannelOpenResponse"));
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // KeyBindingRequest -> KeyBindingResponse(SUCCESS)
    // Ref: InputSourceService.cpp::onKeyBindingRequest()
    // Nota: accettiamo tutti i keycodes — in Phase 5 si filtra via orchestrator.
    // -----------------------------------------------------------------------
    void onKeyBindingRequest(
        const aap_protobuf::service::media::sink::message::KeyBindingRequest &request) override
    {
        std::cout << "[Input] KeyBindingRequest received, keycodes_size="
                  << request.keycodes_size() << std::endl;

        aap_protobuf::service::media::sink::message::KeyBindingResponse response;
        response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
        channel_->sendKeyBindingResponse(response, makePromise("Input/KeyBindingResponse"));
        channel_->receive(this->shared_from_this());
    }

    void onChannelError(const aasdk::error::Error &e) override
    {
        std::cerr << "[Input] Channel Error: " << e.what() << std::endl;
    }

private:
    boost::asio::io_service::strand &strand_;
    aasdk::channel::inputsource::IInputSourceService::Pointer channel_;
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
