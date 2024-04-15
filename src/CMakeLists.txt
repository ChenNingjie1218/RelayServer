include_directories(${CMAKE_SOURCE_DIR}/include)
add_executable(relay_server relay_server.cc)
add_executable(pressure_generator pressure_generator.cc)

add_library(client client.cc)
add_library(server server.cc)

target_link_libraries(relay_server server)
target_link_libraries(pressure_generator client)