file(GLOB TEST_SRC "tests/*.cpp")

include_directories(include)
add_executable(simulator
               ${TEST_SRC}
               main.cpp)


include(ExternalProject)
ExternalProject_Add(gtest
        URL https://github.com/google/googletest/archive/release-1.8.1.zip
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/gtest
        INSTALL_COMMAND ""
        )
ExternalProject_Get_Property(gtest source_dir binary_dir)

add_dependencies(simulator gtest)
include_directories(${source_dir}/googletest/include)

target_link_libraries(simulator ${binary_dir}/googlemock/gtest/libgtest.a)
#target_link_libraries(simulator ${binary_dir}/googlemock/gtest/libgtest_main.a)

##################################
# Just make the test runnable with
#   $ make test

enable_testing()
add_test(NAME    simulator
        COMMAND simulator)