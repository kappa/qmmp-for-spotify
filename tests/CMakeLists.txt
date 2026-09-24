function(spotify_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE spotifylogic Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

spotify_test(tst_spotifylink)
spotify_test(tst_spotifyjson)
