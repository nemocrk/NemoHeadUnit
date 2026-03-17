cmake_minimum_required(VERSION 3.14)
project(NemoHeadUnit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# --- GStreamer (Phase 5) ---
# Moduli richiesti:
#   gstreamer-1.0       -> core pipeline
#   gstreamer-app-1.0   -> GstAppSrc (ingest NAL units da C++)
#   gstreamer-video-1.0 -> GstVideoOverlay (embed nel WId PyQt6)
#
# Install (Debian/Ubuntu):
#   sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
#
# Install (RPi OS / Bookworm):
#   sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
#                    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
#                    gstreamer1.0-libav
pkg_check_modules(GST      REQUIRED gstreamer-1.0>=1.18)
pkg_check_modules(GST_APP  REQUIRED gstreamer-app-1.0>=1.18)
pkg_check_modules(GST_VID  REQUIRED gstreamer-video-1.0>=1.18)

include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v3.0.2
)
FetchContent_MakeAvailable(pybind11)

FetchContent_Declare(
    aasdk
    GIT_REPOSITORY https://github.com/opencardev/aasdk.git
    GIT_TAG        main
)
set(SKIP_BUILD_PROTOBUF ON)
FetchContent_MakeAvailable(aasdk)

# --- Python Bindings ---
#pybind11_add_module(nemo_head_unit
#    src/binding.cpp
#    src/usb/libusb_context.cpp
#    src/usb/usb_hub_manager.cpp
#    # gst_pipeline.hpp e gst_video_sink.hpp sono header-only:
#    # non richiedono una entry .cpp separata.
#)

#target_include_directories(nemo_head_unit PRIVATE
#    ${LIBUSB_INCLUDE_DIRS}
#    ${GST_INCLUDE_DIRS}
#    ${GST_APP_INCLUDE_DIRS}
#    ${GST_VID_INCLUDE_DIRS}
#    ${CMAKE_CURRENT_SOURCE_DIR}/src
##    ${aasdk_SOURCE_DIR}/include
#)
#
#target_link_libraries(nemo_head_unit PRIVATE
#    aasdk
#    Boost::system
#    protobuf::libprotobuf
#    OpenSSL::SSL
#    OpenSSL::Crypto
#    ${LIBUSB_LIBRARIES}
#    ${GST_LIBRARIES}
#    ${GST_APP_LIBRARIES}
#    ${GST_VID_LIBRARIES}
#    Threads::Threads
#)
#
## Propaga le CFLAGS di GStreamer (es. -pthread, macro G_DISABLE_DEPRECATED)
#target_compile_options(nemo_head_unit PRIVATE
#    ${GST_CFLAGS_OTHER}
#    ${GST_APP_CFLAGS_OTHER}
#    ${GST_VID_CFLAGS_OTHER}
#)

# --- AASDK Logger Python Binding (separate module) ---
pybind11_add_module(aasdk_logging
    external_bindings/logger_binding.cpp
)

target_include_directories(aasdk_logging PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(aasdk_logging PRIVATE
    aasdk
)

# CMake helper: generate a tiny wrapper TU for a pybind module.
function(create_pybind_module TARGET_NAME INIT_FUNC)
    set(oneValueArgs HEADER)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(CPM "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(GEN_CPP "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_wrapper.cpp")

    if (CPM_HEADER)
        set(INCLUDE_LINE "#include \"${CPM_HEADER}\"")
    else()
        set(INCLUDE_LINE "")
    endif()

    file(WRITE "${GEN_CPP}" "
#include <pybind11/pybind11.h>
${INCLUDE_LINE}
extern void ${INIT_FUNC}(pybind11::module_&);
PYBIND11_MODULE(${TARGET_NAME}, m) {
    ${INIT_FUNC}(m);
}
")

    pybind11_add_module(${TARGET_NAME} "${GEN_CPP}" ${CPM_SOURCES})
endfunction()

# --- Python Bindings ---
create_pybind_module(protobuf init_protobuf_bindings
    HEADER handlers/common/protobuf_message.hpp
)

target_include_directories(protobuf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(protobuf PRIVATE
    protobuf::libprotobuf
)

create_pybind_module(event init_event_binding
    HEADER handlers/common/event_binding.hpp
)

target_include_directories(event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(event PRIVATE
    Boost::system
    Threads::Threads
)

create_pybind_module(audio_event init_audio_event_handler_binding
    HEADER handlers/audio/audio_event_handler.hpp
)

target_include_directories(audio_event PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VID_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(audio_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VID_LIBRARIES}
    Threads::Threads
)

target_compile_options(audio_event PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
    ${GST_VID_CFLAGS_OTHER}
)

create_pybind_module(video_event init_video_event_handler_binding
    HEADER handlers/video/video_event_handler.hpp
)

target_include_directories(video_event PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VID_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(video_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VID_LIBRARIES}
    Threads::Threads
)

target_compile_options(video_event PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
    ${GST_VID_CFLAGS_OTHER}
)

create_pybind_module(sensor_event init_sensor_event_handler_binding
    HEADER handlers/sensor/sensor_event_handler.hpp
)

target_include_directories(sensor_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(sensor_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(input_event init_input_event_handler_binding
    HEADER handlers/input/input_event_handler.hpp
)

target_include_directories(input_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(input_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(navigation_event init_navigation_event_handler_binding
    HEADER handlers/navigation/navigation_event_handler.hpp
)

target_include_directories(navigation_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(navigation_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(control_event init_control_event_handler_binding
    HEADER handlers/control/control_event_handler.hpp
)

target_include_directories(control_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(control_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(bluetooth_event init_bluetooth_event_handler_binding
    HEADER handlers/bluetooth/bluetooth_event_handler.hpp
)

target_include_directories(bluetooth_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(bluetooth_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(generic_notification_event init_generic_notification_event_handler_binding
    HEADER handlers/generic_notification/generic_notification_event_handler.hpp
)

target_include_directories(generic_notification_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(generic_notification_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(media_browser_event init_media_browser_event_handler_binding
    HEADER handlers/media_browser/media_browser_event_handler.hpp
)

target_include_directories(media_browser_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(media_browser_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(media_playback_status_event init_media_playback_status_event_handler_binding
    HEADER handlers/media_playback_status/media_playback_status_event_handler.hpp
)

target_include_directories(media_playback_status_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(media_playback_status_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(media_source_event init_media_source_event_handler_binding
    HEADER handlers/media_source/media_source_event_handler.hpp
)

target_include_directories(media_source_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VID_INCLUDE_DIRS}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(media_source_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VID_LIBRARIES}
    Threads::Threads
)

target_compile_options(media_source_event PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
    ${GST_VID_CFLAGS_OTHER}
)

create_pybind_module(phone_status_event init_phone_status_event_handler_binding
    HEADER handlers/phone_status/phone_status_event_handler.hpp
)

target_include_directories(phone_status_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(phone_status_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(radio_event init_radio_event_handler_binding
    HEADER handlers/radio/radio_event_handler.hpp
)

target_include_directories(radio_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(radio_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(vendor_extension_event init_vendor_extension_event_handler_binding
    HEADER handlers/vendor_extension/vendor_extension_event_handler.hpp
)

target_include_directories(vendor_extension_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(vendor_extension_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(wifi_projection_event init_wifi_projection_event_handler_binding
    HEADER handlers/wifi_projection/wifi_projection_event_handler.hpp
)

target_include_directories(wifi_projection_event PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(wifi_projection_event PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    Threads::Threads
)

create_pybind_module(av_core init_av_core_binding
    HEADER app/media/av_core.hpp
)

target_include_directories(av_core PRIVATE
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VID_INCLUDE_DIRS}
    ${aasdk_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(av_core PRIVATE
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VID_LIBRARIES}
    aasdk
    Boost::system
    Threads::Threads
)

target_compile_options(av_core PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
    ${GST_VID_CFLAGS_OTHER}
)

create_pybind_module(io_context init_context_runner
    HEADER external_bindings/io_context_runner.hpp
)

target_include_directories(io_context PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(io_context PRIVATE
    Boost::system
    Threads::Threads
)

create_pybind_module(cryptor init_cryptor
    HEADER external_bindings/cryptor.hpp
)

target_include_directories(cryptor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(cryptor PRIVATE
    aasdk
    Boost::system
    Threads::Threads
)

create_pybind_module(transport init_transport
    HEADER external_bindings/transport.hpp
)

target_include_directories(transport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_INCLUDE_DIRS}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(transport PRIVATE
    aasdk
    ${LIBUSB_LIBRARIES}
    Boost::system
    Threads::Threads
)

create_pybind_module(messenger init_messenger
    HEADER external_bindings/messenger.hpp
)

target_include_directories(messenger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(messenger PRIVATE
    aasdk
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::system
    Threads::Threads
)

create_pybind_module(channels init_channels
    HEADER external_bindings/channels.hpp
)

target_include_directories(channels PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(channels PRIVATE
    aasdk
    Boost::system
    Threads::Threads
)

create_pybind_module(aasdk_usb init_aasdk_usb
    HEADER external_bindings/aasdk_usb.hpp
)

target_include_directories(aasdk_usb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBUSB_INCLUDE_DIRS}
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(aasdk_usb PRIVATE
    aasdk
    ${LIBUSB_LIBRARIES}
    Boost::system
    Threads::Threads
)
