cmake_minimum_required(VERSION 3.20)

project(KodeDotRsvpReader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rsvp_text
    components/text/src/chapter.cpp
    components/text/src/display_frame.cpp
    components/text/src/tokenizer.cpp
)
target_include_directories(rsvp_text PUBLIC
    components/text/include
)

add_library(rsvp_playback
    components/playback/src/display_adapter.cpp
    components/playback/src/playback_engine.cpp
)
target_include_directories(rsvp_playback PUBLIC
    components/playback/include
)
target_link_libraries(rsvp_playback PUBLIC rsvp_text)

add_library(rsvp_library
    components/library/src/document_text.cpp
)
target_include_directories(rsvp_library PUBLIC
    components/library/include
)
target_link_libraries(rsvp_library PUBLIC rsvp_text)

add_library(rsvp_storage
    components/storage/src/state_store.cpp
)
target_include_directories(rsvp_storage PUBLIC
    components/storage/include
)

add_library(rsvp_lifecycle
    components/lifecycle/src/reader_lifecycle.cpp
)
target_include_directories(rsvp_lifecycle PUBLIC
    components/lifecycle/include
)
target_link_libraries(rsvp_lifecycle PUBLIC rsvp_playback rsvp_storage)

add_library(rsvp_controller
    components/controller/src/reader_controller.cpp
)
target_include_directories(rsvp_controller PUBLIC
    components/controller/include
)
target_link_libraries(rsvp_controller PUBLIC rsvp_playback)

add_executable(rsvp_host_preview
    apps/host-sim/src/main.cpp
)
target_link_libraries(rsvp_host_preview PRIVATE rsvp_lifecycle rsvp_library)

add_executable(rsvp_frame_dump
    tools/rsvp_frame_dump.cpp
)
target_link_libraries(rsvp_frame_dump PRIVATE rsvp_library rsvp_playback)

include(CTest)
if(BUILD_TESTING)
    add_executable(rsvp_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(rsvp_core_tests PRIVATE rsvp_lifecycle rsvp_library rsvp_controller)
    add_test(NAME rsvp_core_tests COMMAND rsvp_core_tests)
endif()
