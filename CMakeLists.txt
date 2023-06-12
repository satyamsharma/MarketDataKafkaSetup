cmake_minimum_required(VERSION 3.10)

project(market_data_loader)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 17)
set(BUILD_TESTING OFF)

# Third-party CMake
add_subdirectory(third-party/SQLiteCpp)
add_subdirectory(third-party/json)
add_subdirectory(third-party/curl)

# Kafka
set(RDKAFKA_LIB_DIR /usr/lib/x86_64-linux-gnu/)
set(RDKAFKA_INCLUDE_DIR /usr/include/librdkafka/)

# Headers
include_directories(third-party/json/single_include)
include_directories(third-party/curl/include)
include_directories(third-party/SQLiteCpp/include)
include_directories(${RDKAFKA_INCLUDE_DIR})

add_executable(market_data_loader dataloader/market_data_loader.cpp)

target_link_libraries(market_data_loader
    SQLiteCpp
    libcurl
    ${RDKAFKA_LIB_DIR}/librdkafka++.a
    ${RDKAFKA_LIB_DIR}/librdkafka.a
    ssl
    crypto
    z
    sasl2
    zstd
    lz4
)
