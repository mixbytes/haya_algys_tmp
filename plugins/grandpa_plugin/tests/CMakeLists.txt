set( CMAKE_CXX_STANDARD 14 )

add_executable( grandpa_plugin_unit_test main.cpp grandpa_plugin_tests.cpp )
target_link_libraries( grandpa_plugin_unit_test grandpa_plugin eosio_chain chainbase eosio_testing fc )