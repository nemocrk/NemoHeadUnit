cmake_minimum_required(VERSION 3.10)
project(nemo_head_unit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- IMPOSTAZIONI LOGGER AASDK ---
# Abilita il livello di log DEBUG in aasdk per avere tracciamento profondo del Cryptor e Handshake
set(AASDK_LOG_LEVEL "DEBUG" CACHE STRING "AASDK logging level" FORCE)
add_definitions(-DAASDK_DEBUG_LOGGING)
# ---------------------------------

include(FetchContent)

find_package(Boost REQUIRED COMPONENTS system thread asio)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(OPENSSL REQUIRED openssl)

# 1. AASDK
FetchContent_Declare(
    aasdk
    GIT_REPOSITORY https://github.com/f1xpl/aasdk.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(aasdk)

# 2. Pybind11
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ${LIBUSB_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${aasdk_SOURCE_DIR}/include
    ${aasdk_BINARY_DIR}/protobuf
)

# Nota: rimosso pybind11_protobuf dalla build perché non più necessario se passiamo byte serializzati

pybind11_add_module(nemo_head_unit
    src/binding.cpp
    src/usb/libusb_context.cpp
    src/usb/usb_hub_manager.cpp
)

target_link_libraries(nemo_head_unit PRIVATE
    ${Boost_LIBRARIES}
    ${LIBUSB_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    aasdk
)
