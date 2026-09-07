cmake_minimum_required(VERSION 3.21)
project(VeilKnitCppSdk VERSION 0.1.0 LANGUAGES CXX)

option(VEILKNIT_BUILD_EXAMPLES "Build VeilKnit SDK examples" ON)
option(VEILKNIT_BUILD_TESTS "Build VeilKnit SDK tests" ON)

add_library(veilknit_cpp STATIC
    src/veilknit.cpp
)
add_library(VeilKnit::cpp ALIAS veilknit_cpp)

target_include_directories(veilknit_cpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(veilknit_cpp PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(veilknit_cpp PRIVATE /W4 /permissive- /EHsc
        $<$<CONFIG:Release>:/pathmap:${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/sdk>)
    target_link_options(veilknit_cpp PRIVATE $<$<CONFIG:Release>:/Brepro>)
    target_link_libraries(veilknit_cpp PRIVATE bcrypt)
else()
    target_compile_options(veilknit_cpp PRIVATE -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/sdk>
        $<$<CONFIG:Release>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/sdk>
        $<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/sdk>)
endif()

if(VEILKNIT_BUILD_EXAMPLES)
    add_executable(veilknit_hello examples/hello.cpp)
    target_link_libraries(veilknit_hello PRIVATE VeilKnit::cpp)

    add_executable(veilknit_room_primitives examples/room_primitives.cpp)
    target_link_libraries(veilknit_room_primitives PRIVATE VeilKnit::cpp)
endif()

if(VEILKNIT_BUILD_TESTS)
    include(CTest)
    add_executable(veilknit_codec_tests tests/codec_tests.cpp)
    target_link_libraries(veilknit_codec_tests PRIVATE VeilKnit::cpp)
    add_test(NAME veilknit_codec_tests COMMAND veilknit_codec_tests)
endif()

include(GNUInstallDirs)
install(TARGETS veilknit_cpp EXPORT VeilKnitCppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT VeilKnitCppTargets
    FILE VeilKnitCppTargets.cmake
    NAMESPACE VeilKnit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/VeilKnitCpp)
