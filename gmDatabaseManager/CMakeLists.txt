cmake_minimum_required(VERSION 3.16.3)
project(gmDatabaseManager LANGUAGES CXX)
file(GLOB cppFiles "source/*.cpp")

include_directories(include)

add_library(${PROJECT_NAME} STATIC ${cppFiles})
set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)