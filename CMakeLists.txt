cmake_minimum_required(VERSION 3.14)
project(NemoHeadUnit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Dipendenze di sistema richieste
find_package(Boost REQUIRED COMPONENTS system)
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

include(FetchContent)

# Pybind11 per il bridge C++ -> Python
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.11.1
)
FetchContent_MakeAvailable(pybind11)

# Aasdk come dipendenza diretta
FetchContent_Declare(
    aasdk
    GIT_REPOSITORY https://github.com/f1xpl/aasdk.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(aasdk)

# Modulo Python C++ (nemo_head_unit)
pybind11_add_module(nemo_head_unit
    src/binding.cpp
)

target_include_directories(nemo_head_unit PRIVATE 
    ${LIBUSB_INCLUDE_DIRS}
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