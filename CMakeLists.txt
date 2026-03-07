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
)
target_include_directories(nemo_head_unit PRIVATE 
    ${LIBUSB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(nemo_head_unit PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBUSB_LIBRARIES}
    Threads::Threads
)
