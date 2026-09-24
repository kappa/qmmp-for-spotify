add_executable(spotify-cli spotify-cli.cpp)
target_link_libraries(spotify-cli PRIVATE qmmp-spotify-core)
