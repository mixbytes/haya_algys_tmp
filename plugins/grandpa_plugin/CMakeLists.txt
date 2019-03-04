file(GLOB HEADERS "include/eosio/grandpa_plugin/*.hpp")
add_library( grandpa_plugin
             grandpa_plugin.cpp
             ${HEADERS} )

target_link_libraries( grandpa_plugin appbase fc )
target_include_directories( grandpa_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
