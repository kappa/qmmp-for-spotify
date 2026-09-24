add_library(qmmp-spotify-core SHARED
    spotifyauth.cpp
)
target_include_directories(qmmp-spotify-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qmmp-spotify-core PUBLIC spotifylogic Qt6::Gui Qt6::Network Qt6::NetworkAuth)
