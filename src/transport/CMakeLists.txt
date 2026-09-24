add_library(spotifytransport MODULE
    spotifyinputfactory.cpp
    spotifyinputfactory.h
    spotifyinputsource.h
)
target_link_libraries(spotifytransport PRIVATE PkgConfig::QMMP Qt6::Widgets)
set_target_properties(spotifytransport PROPERTIES OUTPUT_NAME spotify)
install(TARGETS spotifytransport LIBRARY DESTINATION ${QMMP_PLUGIN_DIR}/Transports)
