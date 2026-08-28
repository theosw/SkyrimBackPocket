cmake_minimum_required(VERSION 3.24)

project(BackPocket VERSION 0.2.2 LANGUAGES CXX)

option(BACK_POCKET_BUILD_TESTS "Build Back Pocket unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE tests" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build for Skyrim AE 1.6.1170" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build for Skyrim SE 1.5.97" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Do not build the Skyrim VR target" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "Back Pocket does not use Xbyak" FORCE)

set(back_pocket_default_commonlib "$ENV{BACK_POCKET_COMMONLIBSSE_NG_DIR}")
if(back_pocket_default_commonlib STREQUAL "")
  set(back_pocket_default_commonlib "$ENV{CommonLibSSEPath}")
endif()
set(
  BACK_POCKET_COMMONLIBSSE_NG_DIR
  "${back_pocket_default_commonlib}"
  CACHE PATH
  "Path to a CommonLibSSE-NG checkout"
)

if(NOT EXISTS "${BACK_POCKET_COMMONLIBSSE_NG_DIR}/CMakeLists.txt")
  message(
    FATAL_ERROR
    "CommonLibSSE-NG was not found at '${BACK_POCKET_COMMONLIBSSE_NG_DIR}'. "
    "Set BACK_POCKET_COMMONLIBSSE_NG_DIR or the CommonLibSSEPath environment variable."
  )
endif()

add_subdirectory(
  "${BACK_POCKET_COMMONLIBSSE_NG_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/CommonLibSSE"
  EXCLUDE_FROM_ALL
)
if(MSVC)
  target_compile_options(CommonLibSSE PRIVATE /FS)
endif()

configure_file(
  cmake/plugin_version.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin_version.h"
  @ONLY
)

add_library(
  back_pocket_core
  STATIC
  include/back_pocket/category_policy.h
  include/back_pocket/form_id_policy.h
  include/back_pocket/input_binding.h
  include/back_pocket/pocket.h
  include/back_pocket/quick_item_transfer_policy.h
  include/back_pocket/saved_state.h
  include/back_pocket/tab_transition_policy.h
  src/input_binding.cpp
  src/pocket.cpp
  src/saved_state.cpp
)
target_include_directories(back_pocket_core PUBLIC include)
target_compile_features(back_pocket_core PUBLIC cxx_std_23)
set_target_properties(back_pocket_core PROPERTIES FOLDER "Libraries")

add_library(
  BackPocket
  SHARED
  src/config.cpp
  src/config.h
  src/inventory_filter.cpp
  src/inventory_filter.h
  src/item_menu_footer.cpp
  src/item_menu_footer.h
  src/pch.h
  src/persistence.cpp
  src/persistence.h
  src/plugin.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/generated/plugin_version.h"
)
target_precompile_headers(BackPocket PRIVATE src/pch.h)
target_include_directories(
  BackPocket
  PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  include
  src
)
target_link_libraries(
  BackPocket
  PRIVATE
  CommonLibSSE::CommonLibSSE
  back_pocket_core
)
target_compile_features(BackPocket PRIVATE cxx_std_23)
target_compile_definitions(BackPocket PRIVATE UNICODE _UNICODE)
set_target_properties(BackPocket PROPERTIES FOLDER "Plugin")

if(MSVC)
  foreach(target back_pocket_core BackPocket)
    target_compile_options(
      ${target}
      PRIVATE
      /MP
      /W4
      /permissive-
      /Zc:__cplusplus
      /utf-8
    )
  endforeach()
endif()

if(BACK_POCKET_BUILD_TESTS)
  enable_testing()
  find_package(Catch2 3 CONFIG REQUIRED)
  add_executable(back_pocket_tests tests/back_pocket_tests.cpp)
  target_link_libraries(back_pocket_tests PRIVATE back_pocket_core Catch2::Catch2WithMain)
  target_compile_features(back_pocket_tests PRIVATE cxx_std_23)
  if(MSVC)
    target_compile_options(back_pocket_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
  endif()
  add_test(NAME back_pocket_tests COMMAND back_pocket_tests)
  set_target_properties(back_pocket_tests PROPERTIES FOLDER "Tests")
endif()

set(
  back_pocket_category_icon
  "${CMAKE_CURRENT_SOURCE_DIR}/package/BackPocket/Interface/BackPocket/category_icon.swf"
)
add_custom_target(
  back_pocket_assets
  COMMAND
    powershell.exe -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_category_icon.ps1"
    -output_path "${back_pocket_category_icon}"
  BYPRODUCTS "${back_pocket_category_icon}"
  COMMENT "Generating the Back Pocket category icon"
)
set_target_properties(back_pocket_assets PROPERTIES FOLDER "Assets")

set(back_pocket_stage_plugins "${CMAKE_CURRENT_SOURCE_DIR}/package/BackPocket/SKSE/Plugins")
add_custom_target(
  stage
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${back_pocket_stage_plugins}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:BackPocket>"
          "${back_pocket_stage_plugins}/$<TARGET_FILE_NAME:BackPocket>"
  DEPENDS BackPocket back_pocket_assets
  COMMENT "Staging Back Pocket as an MO2-ready mod"
)
set_target_properties(stage PROPERTIES FOLDER "Packaging")

set(
  BACK_POCKET_DEPLOY_DIR
  ""
  CACHE PATH
  "Optional MO2 mod folder used for development deployment"
)
if(NOT BACK_POCKET_DEPLOY_DIR STREQUAL "")
  set(back_pocket_deploy_plugins "${BACK_POCKET_DEPLOY_DIR}/SKSE/Plugins")
  set(back_pocket_deploy_interface "${BACK_POCKET_DEPLOY_DIR}/Interface/BackPocket")
  add_custom_target(
    deploy
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${back_pocket_deploy_plugins}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${back_pocket_deploy_interface}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:BackPocket>"
            "${back_pocket_deploy_plugins}/$<TARGET_FILE_NAME:BackPocket>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/package/BackPocket/SKSE/Plugins/BackPocket.ini"
            "${back_pocket_deploy_plugins}/BackPocket.ini"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${back_pocket_category_icon}"
            "${back_pocket_deploy_interface}/category_icon.swf"
    DEPENDS BackPocket back_pocket_assets
    COMMENT "Deploying Back Pocket to ${BACK_POCKET_DEPLOY_DIR}"
  )
  set_target_properties(deploy PROPERTIES FOLDER "Packaging")
  message(STATUS "Development deploy directory: ${back_pocket_deploy_plugins}")
else()
  message(STATUS "Development deployment disabled")
endif()
