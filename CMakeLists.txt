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
pybind11_add_module(nemo_head_unit
    src/binding.cpp
    src/crypto/crypto_manager.cpp
    src/usb/libusb_context.cpp
    src/usb/usb_hub_manager.cpp
    # gst_pipeline.hpp e gst_video_sink.hpp sono header-only:
    # non richiedono una entry .cpp separata.
)

target_include_directories(nemo_head_unit PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VID_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${aasdk_SOURCE_DIR}/include
)

target_link_libraries(nemo_head_unit PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBUSB_LIBRARIES}
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VID_LIBRARIES}
    Threads::Threads
)

# Propaga le CFLAGS di GStreamer (es. -pthread, macro G_DISABLE_DEPRECATED)
target_compile_options(nemo_head_unit PRIVATE
    ${GST_CFLAGS_OTHER}
    ${GST_APP_CFLAGS_OTHER}
    ${GST_VID_CFLAGS_OTHER}
)
