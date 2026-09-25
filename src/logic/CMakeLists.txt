add_library(spotifylogic STATIC
    spotifylink.cpp
    spotifyjson.cpp
    playlistfilter.cpp
    apipolicy.cpp
    enginelogic.cpp
    volumelogic.cpp
    followerlogic.cpp
    pollpolicy.cpp
)
target_include_directories(spotifylogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spotifylogic PUBLIC Qt6::Core)
