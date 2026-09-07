cmake_minimum_required(VERSION 3.24)
project(VeilKnitRooms VERSION 0.5.0 LANGUAGES CXX)

option(VKROOMS_BUILD_TESTS "Build VeilKnit Rooms core tests" ON)
option(VKROOMS_BUILD_LINUX_GUI "Build the GTK 3 Linux GUI when GTK is available" ON)
option(VKROOMS_PRIVACY_RELEASE "Remap source paths and enable reproducible release flags" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

set(VEILKNIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VEILKNIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/veilknit_cpp)

find_package(Threads REQUIRED)
if(NOT WIN32)
    find_package(OpenSSL REQUIRED)
endif()

add_library(veilknit_rooms_core STATIC
    src/util.cpp
    src/crypto.cpp
    src/room_protocol.cpp
    src/persistence.cpp
    src/room_engine.cpp
)
target_include_directories(veilknit_rooms_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(veilknit_rooms_core PUBLIC cxx_std_17)
target_link_libraries(veilknit_rooms_core PUBLIC VeilKnit::cpp Threads::Threads)
if(NOT WIN32)
    target_link_libraries(veilknit_rooms_core PUBLIC OpenSSL::Crypto)
endif()

function(vkrooms_harden target logical_name)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
        if(VKROOMS_PRIVACY_RELEASE)
            target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/pathmap:${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/${logical_name}>)
            target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/Brepro>)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(VKROOMS_PRIVACY_RELEASE)
            target_compile_options(${target} PRIVATE
                $<$<CONFIG:Release>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/${logical_name}>
                $<$<CONFIG:Release>:-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/${logical_name}>
                $<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=/_/veilknit-rooms/${logical_name}>)
        endif()
    endif()
endfunction()
vkrooms_harden(veilknit_rooms_core core)

if(WIN32)
    enable_language(RC)
    # Vendored v1.8.0 source avoids an unverified configure-time download.
    add_library(veilknit_qrcodegen STATIC third_party/qrcodegen/qrcodegen.cpp)
    target_include_directories(veilknit_qrcodegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen)
    target_compile_features(veilknit_qrcodegen PUBLIC cxx_std_17)
    vkrooms_harden(veilknit_qrcodegen qrcode)

    add_executable(VeilKnitRooms WIN32 src/win32_app.cpp resources/VeilKnitRooms.rc)
    set_source_files_properties(resources/VeilKnitRooms.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/VeilKnitRooms.manifest")
    target_include_directories(VeilKnitRooms PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    target_compile_definitions(VeilKnitRooms PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(VeilKnitRooms PRIVATE veilknit_rooms_core veilknit_qrcodegen bcrypt comctl32 dwmapi uxtheme)
    vkrooms_harden(VeilKnitRooms windows-gui)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VeilKnitRooms)
else()
    add_executable(veilknit-rooms-console src/linux_console.cpp)
    target_link_libraries(veilknit-rooms-console PRIVATE veilknit_rooms_core)
    vkrooms_harden(veilknit-rooms-console linux-console)

    if(VKROOMS_BUILD_LINUX_GUI)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 gtk+-3.0)
        if(GTK3_FOUND)
            add_executable(veilknit-rooms-gui src/linux_gtk_app.cpp)
            target_include_directories(veilknit-rooms-gui PRIVATE ${GTK3_INCLUDE_DIRS})
            target_compile_options(veilknit-rooms-gui PRIVATE ${GTK3_CFLAGS_OTHER})
            target_link_directories(veilknit-rooms-gui PRIVATE ${GTK3_LIBRARY_DIRS})
            target_link_libraries(veilknit-rooms-gui PRIVATE veilknit_rooms_core ${GTK3_LIBRARIES})
            vkrooms_harden(veilknit-rooms-gui linux-gui)
        else()
            message(WARNING "GTK 3 development files were not found; building console Rooms only.")
        endif()
    endif()
endif()

if(VKROOMS_BUILD_TESTS)
    include(CTest)
    add_executable(veilknit_rooms_core_tests tests/core_tests.cpp)
    target_link_libraries(veilknit_rooms_core_tests PRIVATE veilknit_rooms_core)
    vkrooms_harden(veilknit_rooms_core_tests tests)
    add_test(NAME veilknit_rooms_core_tests COMMAND veilknit_rooms_core_tests)
endif()
