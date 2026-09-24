add_library(spotifylogic STATIC
    spotifylink.cpp
    spotifyjson.cpp
    apipolicy.cpp
)
target_include_directories(spotifylogic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(spotifylogic PUBLIC Qt6::Core)
