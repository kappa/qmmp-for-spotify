add_library(spotifyengine MODULE
    spotifyenginefactory.cpp
    spotifyengine.cpp
)
target_link_libraries(spotifyengine PRIVATE qmmp-spotify-core PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifyengine PROPERTIES OUTPUT_NAME spotify INSTALL_RPATH "$ORIGIN/..")
install(TARGETS spotifyengine LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/Engines)
