file(GLOB HEADERS "include/eosio/grandpa_plugin/*.hpp")
add_library( grandpa_plugin
             grandpa_plugin.cpp
             ${HEADERS} )

target_link_libraries( grandpa_plugin chain_plugin eosio_chain appbase fc bnet_plugin)
target_include_directories( grandpa_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
add_subdirectory(tests)
