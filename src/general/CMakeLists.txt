add_library(spotifygeneral MODULE
    spotifygeneralfactory.cpp
    spotifygeneral.cpp
    settingsdialog.cpp
    devicedialog.cpp
)
target_link_libraries(spotifygeneral PRIVATE qmmp-spotify-core PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifygeneral PROPERTIES OUTPUT_NAME spotify INSTALL_RPATH "$ORIGIN/..")
install(TARGETS spotifygeneral LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/General)
