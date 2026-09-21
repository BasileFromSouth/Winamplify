cmake_minimum_required(VERSION 3.20)
project(Winamplify LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "Winamp plugins are Windows-only")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(WARNING "Winamp 5.9.2 stock is 32-bit. Configure with: cmake -A Win32")
endif()

set(SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/winamp_sdk")
set(COMMON_DIR "${CMAKE_SOURCE_DIR}/plugin/common")

add_library(spotify_common STATIC
  plugin/common/utf8.cpp
  plugin/common/json.cpp
  plugin/common/http.cpp
  plugin/common/pkce.cpp
  plugin/common/store.cpp
  plugin/common/oauth_loopback.cpp
  plugin/common/spotify_api.cpp
  plugin/common/cover_art.cpp
)
target_include_directories(spotify_common PUBLIC "${COMMON_DIR}")
target_compile_definitions(spotify_common PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(spotify_common PUBLIC winhttp bcrypt ws2_32 shell32)
if(MSVC)
  target_compile_options(spotify_common PUBLIC /utf-8 /W3)
  set_property(TARGET spotify_common PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(gen_spotify SHARED
  plugin/gen_spotify/plugin.cpp
  plugin/gen_spotify/vis_loopback.cpp
  plugin/gen_spotify/plugin.rc
)
target_include_directories(gen_spotify PRIVATE "${SDK_DIR}" "${COMMON_DIR}")
target_link_libraries(gen_spotify PRIVATE spotify_common comctl32 ole32 uuid psapi avrt mmdevapi)
set_target_properties(gen_spotify PROPERTIES PREFIX "" OUTPUT_NAME gen_spotify)
if(MSVC)
  set_property(TARGET gen_spotify PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(gen_spotify PRIVATE /EXPORT:winampGetGeneralPurposePlugin /INCREMENTAL:NO)
endif()

add_library(in_winamplify SHARED plugin/in_winamplify/plugin.cpp)
target_include_directories(in_winamplify PRIVATE "${SDK_DIR}")
target_link_libraries(in_winamplify PRIVATE ole32 uuid)
set_target_properties(in_winamplify PROPERTIES PREFIX "" OUTPUT_NAME in_winamplify)
if(MSVC)
  set_property(TARGET in_winamplify PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(in_winamplify PRIVATE /EXPORT:winampGetInModule2 /EXPORT:winamplify_vis_pump /INCREMENTAL:NO)
endif()

add_library(ml_spotify SHARED plugin/ml_spotify/plugin.cpp)
target_include_directories(ml_spotify PRIVATE "${SDK_DIR}" "${COMMON_DIR}")
target_link_libraries(ml_spotify PRIVATE spotify_common comctl32)
set_target_properties(ml_spotify PROPERTIES PREFIX "" OUTPUT_NAME ml_spotify)
if(MSVC)
  set_property(TARGET ml_spotify PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_link_options(ml_spotify PRIVATE /EXPORT:winampGetMediaLibraryPlugin /INCREMENTAL:NO)
endif()

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
foreach(plugin_tgt IN ITEMS gen_spotify ml_spotify in_winamplify)
  add_custom_command(TARGET ${plugin_tgt} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin_tgt}>" "${DIST_DIR}/"
  )
endforeach()
