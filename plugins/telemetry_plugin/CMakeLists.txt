file(GLOB HEADERS "include/eosio/telemetry_plugin/*.hpp")
add_library( telemetry_plugin
             telemetry_plugin.cpp
             ${HEADERS} )

target_link_libraries( telemetry_plugin chain_plugin eosio_chain appbase fc )
target_include_directories( telemetry_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
