cmake_minimum_required(VERSION 2.6)
project(LatencyTest)

include_directories(${Boost_INCLUDE_DIRS})
link_directories(${Boost_LIBRARY_DIRS})
link_directories(${Boost_LIBRARYDIR})

file(GLOB HeaderFiles ./*.h )
file(GLOB SourceFiles ./*.cpp)
add_executable(${PROJECT_NAME} ${SourceFiles} ${HeaderFiles})

target_link_libraries(${PROJECT_NAME} kNet ${Boost_LIBRARIES})