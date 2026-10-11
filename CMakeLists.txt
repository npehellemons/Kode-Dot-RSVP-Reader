cmake_minimum_required(VERSION 3.20)

project(KodeDotRsvpReader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)

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

add_library(rsvp_controller
    components/controller/src/reader_controller.cpp
)
target_include_directories(rsvp_controller PUBLIC
    components/controller/include
)
target_link_libraries(rsvp_controller PUBLIC rsvp_playback)

add_library(rsvp_lifecycle
    components/lifecycle/src/reader_lifecycle.cpp
)
target_include_directories(rsvp_lifecycle PUBLIC
    components/lifecycle/include
)
target_link_libraries(rsvp_lifecycle PUBLIC rsvp_controller rsvp_playback rsvp_storage)

add_library(rsvp_platform
    components/platform/src/input_contract.cpp
)
target_include_directories(rsvp_platform PUBLIC
    components/platform/include
)
target_link_libraries(rsvp_platform PUBLIC rsvp_controller)

add_library(rsvp_runtime
    components/runtime/src/reader_runtime.cpp
)
target_include_directories(rsvp_runtime PUBLIC
    components/runtime/include
)
target_link_libraries(rsvp_runtime PUBLIC rsvp_lifecycle rsvp_platform)

add_library(rsvp_ui
    components/ui/src/reader_view_model.cpp
)
target_include_directories(rsvp_ui PUBLIC
    components/ui/include
)
target_link_libraries(rsvp_ui PUBLIC rsvp_controller)

add_library(kode_dot_app
    apps/kode-dot/src/reader_app.cpp
    apps/kode-dot/src/reader_boot.cpp
    apps/kode-dot/src/reader_document.cpp
    apps/kode-dot/src/reader_host.cpp
    apps/kode-dot/src/reader_input.cpp
    apps/kode-dot/src/reader_lifecycle.cpp
    apps/kode-dot/src/reader_screen.cpp
    apps/kode-dot/src/reader_storage.cpp
)
target_include_directories(kode_dot_app PUBLIC
    apps/kode-dot/include
)
target_link_libraries(kode_dot_app PUBLIC rsvp_library rsvp_runtime rsvp_ui)

add_executable(rsvp_host_preview
    apps/host-sim/src/main.cpp
)
target_link_libraries(rsvp_host_preview PRIVATE rsvp_library rsvp_runtime rsvp_ui)

add_executable(kode_dot_smoke
    apps/kode-dot/src/smoke_main.cpp
)
target_link_libraries(kode_dot_smoke PRIVATE kode_dot_app)

add_executable(rsvp_frame_dump
    tools/rsvp_frame_dump.cpp
)
target_link_libraries(rsvp_frame_dump PRIVATE rsvp_library rsvp_playback)

add_executable(rsvp_controller_trace
    tools/rsvp_controller_trace.cpp
)
target_link_libraries(rsvp_controller_trace PRIVATE rsvp_controller)

add_executable(rsvp_view_model_trace
    tools/rsvp_view_model_trace.cpp
)
target_link_libraries(rsvp_view_model_trace PRIVATE rsvp_ui)

include(CTest)
if(BUILD_TESTING)
    add_executable(rsvp_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(rsvp_core_tests PRIVATE
        kode_dot_app
        rsvp_library
        rsvp_runtime
        rsvp_ui
    )
    add_test(NAME rsvp_core_tests COMMAND rsvp_core_tests)
    add_test(NAME kode_dot_smoke COMMAND kode_dot_smoke)
    add_test(NAME rsvp_controller_trace COMMAND rsvp_controller_trace)
    add_test(NAME rsvp_view_model_trace COMMAND rsvp_view_model_trace)
    if(Python3_FOUND)
        add_test(
            NAME device_sim_controller_trace
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_device_sim_controller_trace.py
                --controller-trace $<TARGET_FILE:rsvp_controller_trace>
                --frame-dump $<TARGET_FILE:rsvp_frame_dump>
        )
        add_test(
            NAME device_sim_view_model_trace
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_device_sim_view_model_trace.py
                --view-model-trace $<TARGET_FILE:rsvp_view_model_trace>
                --frame-dump $<TARGET_FILE:rsvp_frame_dump>
        )
    endif()
endif()
