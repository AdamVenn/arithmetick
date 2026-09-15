cmake_minimum_required(VERSION 3.25)
project(ARITHMETICK VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64 x86_64)

add_library(arithmetick INTERFACE)

target_include_directories(arithmetick INTERFACE ${PROJECT_SOURCE_DIR})

