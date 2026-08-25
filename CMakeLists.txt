# ---------------------------------------------------------------------------
#  Haven: Deep Shelter — a 3D underground shelter-management game
#  Apple Silicon (arm64) macOS only.  See dist/users/install.sh.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

# The app is Apple-Silicon-only; the portable core additionally builds
# anywhere so the simulation test-suite can run in CI containers.
project(Haven VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HAVEN_BUILD_APP     "Build the windowed game (requires macOS arm64)" ON)
option(HAVEN_BUILD_TESTS   "Build the headless simulation test-suite"       ON)
option(HAVEN_ENABLE_VULKAN "Build the Vulkan/MoltenVK backend when possible" OFF)
set(HAVEN_VULKAN_HEADERS   "" CACHE PATH "Directory containing vulkan/vulkan.h")
set(HAVEN_GLSLANG          "" CACHE FILEPATH "glslangValidator used to bake SPIR-V")

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
  endif()
endif()

if(MSVC)
  message(FATAL_ERROR "Haven targets Apple Silicon macOS only.")
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3 -fno-math-errno)
  if(APPLE)
    # Baseline for every shipping Apple Silicon part (M1 and newer).
    add_compile_options(-mcpu=apple-m1)
  endif()
endif()

# ---------------------------------------------------------------------------
#  haven_core — platform independent: math, ECS, simulation, gameplay, save.
#  No windowing, no GPU.  This is what the test-suite exercises.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE HAVEN_CORE_SRC CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/ecs/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/physics/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/ai/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/sim/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/gameplay/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/save/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/anim/*.cpp"
)
# Procedural mesh generation is pure math with no GPU calls, so it lives in
# the portable core and its geometry (winding, normals) is unit-testable
# without a window or a GL context.
list(APPEND HAVEN_CORE_SRC "${CMAKE_SOURCE_DIR}/src/scene/Primitives.cpp")
add_library(haven_core STATIC ${HAVEN_CORE_SRC})
target_include_directories(haven_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
if(APPLE)
  target_sources(haven_core PRIVATE "${CMAKE_SOURCE_DIR}/src/core/PathsApple.mm")
  target_link_libraries(haven_core PUBLIC "-framework Foundation")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/core/PathsApple.mm"
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
  target_sources(haven_core PRIVATE "${CMAKE_SOURCE_DIR}/src/core/PathsPosix.cpp")
endif()
find_package(Threads REQUIRED)
target_link_libraries(haven_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
#  Shader baking: GLSL 4.1 for the OpenGL backend is embedded as text;
#  SPIR-V for Vulkan is baked when a glslang compiler can be found.
# ---------------------------------------------------------------------------
set(HAVEN_HAVE_VULKAN OFF)
if(APPLE AND HAVEN_ENABLE_VULKAN)
  set(_vk_inc "")
  if(HAVEN_VULKAN_HEADERS AND EXISTS "${HAVEN_VULKAN_HEADERS}/vulkan/vulkan.h")
    set(_vk_inc "${HAVEN_VULKAN_HEADERS}")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include/vulkan/vulkan.h")
    set(_vk_inc "${CMAKE_SOURCE_DIR}/third_party/Vulkan-Headers/include")
  elseif(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/include/vulkan/vulkan.h")
    set(_vk_inc "$ENV{VULKAN_SDK}/include")
  endif()
  if(_vk_inc)
    set(HAVEN_HAVE_VULKAN ON)
    set(HAVEN_VK_INCLUDE "${_vk_inc}")
    message(STATUS "Haven: Vulkan headers at ${_vk_inc} — MoltenVK backend enabled")
  else()
    message(STATUS "Haven: no Vulkan headers — building OpenGL-only")
  endif()
endif()

set(HAVEN_SPV_DIR "${CMAKE_BINARY_DIR}/spv")
set(HAVEN_GENERATED "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HAVEN_GENERATED}")

if(HAVEN_HAVE_VULKAN)
  set(_glslang "${HAVEN_GLSLANG}")
  if(NOT _glslang)
    find_program(_glslang NAMES glslangValidator glslang
                 HINTS "$ENV{VULKAN_SDK}/bin" "${CMAKE_SOURCE_DIR}/third_party/glslang/bin")
  endif()
  if(_glslang)
    file(MAKE_DIRECTORY "${HAVEN_SPV_DIR}")
    set(_spv_outputs "")
    foreach(_stage vert frag)
      foreach(_name scene ui post)
        set(_in  "${CMAKE_SOURCE_DIR}/shaders/vk/${_name}.${_stage}")
        if(EXISTS "${_in}")
          set(_out "${HAVEN_SPV_DIR}/${_name}.${_stage}.spv")
          add_custom_command(OUTPUT "${_out}"
            COMMAND "${_glslang}" -V "${_in}" -o "${_out}"
            DEPENDS "${_in}" VERBATIM)
          list(APPEND _spv_outputs "${_out}")
        endif()
      endforeach()
    endforeach()
    add_custom_target(haven_spirv DEPENDS ${_spv_outputs})
  else()
    message(STATUS "Haven: glslang not found — Vulkan backend will report itself unavailable")
    add_custom_target(haven_spirv)
  endif()
endif()

# ---------------------------------------------------------------------------
#  The game application (macOS arm64 only).
# ---------------------------------------------------------------------------
if(HAVEN_BUILD_APP)
  if(NOT APPLE)
    message(STATUS "Haven: not macOS — skipping the app target, core + tests only")
    set(HAVEN_BUILD_APP OFF)
  elseif(NOT CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    message(FATAL_ERROR "Haven supports Apple Silicon (arm64) only.")
  endif()
endif()

if(HAVEN_BUILD_APP)
  file(GLOB_RECURSE HAVEN_APP_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/renderer/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/renderer/gl/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/assets/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/scene/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/audio/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/input/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/debug/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/game/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/platform/*.cpp"
  )
  list(APPEND HAVEN_APP_SRC
    "${CMAKE_SOURCE_DIR}/src/platform/macos/CocoaWindow.mm"
    "${CMAKE_SOURCE_DIR}/src/platform/macos/MacSystemInfo.mm"
    "${CMAKE_SOURCE_DIR}/src/audio/CoreAudioBackend.mm"
    "${CMAKE_SOURCE_DIR}/src/game/main.mm"
  )
  # Primitives.cpp is already compiled into haven_core, which the app links.
  list(REMOVE_ITEM HAVEN_APP_SRC "${CMAKE_SOURCE_DIR}/src/scene/Primitives.cpp")
  list(REMOVE_DUPLICATES HAVEN_APP_SRC)
  if(HAVEN_HAVE_VULKAN)
    # The Vulkan/MoltenVK backend (src/renderer/vk/) is not yet implemented —
    # detection and SPIR-V baking are wired up above for when it lands, but
    # until then the app always falls back to the OpenGL 4.1 backend, which
    # RenderDeviceFactory.cpp does automatically and honestly (it never
    # fakes a Vulkan device). Guard against a stale cache value claiming
    # Vulkan is available with no backend sources to prove it.
    file(GLOB HAVEN_VK_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/renderer/vk/*.cpp")
    if(NOT HAVEN_VK_SRC)
      message(STATUS "Haven: Vulkan headers found but the backend source (src/renderer/vk/) "
                      "doesn't exist yet — building OpenGL-only")
      set(HAVEN_HAVE_VULKAN OFF)
    else()
      list(APPEND HAVEN_APP_SRC ${HAVEN_VK_SRC})
    endif()
  endif()

  add_executable(Haven MACOSX_BUNDLE ${HAVEN_APP_SRC})
  target_link_libraries(Haven PRIVATE haven_core)
  target_include_directories(Haven PRIVATE "${CMAKE_SOURCE_DIR}/src" "${HAVEN_GENERATED}")
  target_compile_definitions(Haven PRIVATE
    HAVEN_VERSION="${PROJECT_VERSION}"
    GL_SILENCE_DEPRECATION=1)
  target_link_libraries(Haven PRIVATE
    "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL"
    "-framework CoreVideo" "-framework AudioToolbox" "-framework CoreText"
    "-framework CoreGraphics" "-framework Metal" "-framework IOKit")

  foreach(_f ${HAVEN_APP_SRC})
    if(_f MATCHES "\\.mm$")
      set_source_files_properties(${_f} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()
  endforeach()

  if(HAVEN_HAVE_VULKAN)
    target_compile_definitions(Haven PRIVATE HAVEN_VULKAN=1
      HAVEN_SPV_DIR="${HAVEN_SPV_DIR}")
    target_include_directories(Haven PRIVATE "${HAVEN_VK_INCLUDE}")
    add_dependencies(Haven haven_spirv)
  endif()

  set_target_properties(Haven PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Haven"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.haven.deepshelter"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in")

  # Shaders and baked SPIR-V travel inside the bundle's Resources folder.
  add_custom_command(TARGET Haven POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/shaders"
            "$<TARGET_BUNDLE_CONTENT_DIR:Haven>/Resources/shaders")
  if(HAVEN_HAVE_VULKAN)
    add_custom_command(TARGET Haven POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:Haven>/Resources/spv"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${HAVEN_SPV_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:Haven>/Resources/spv")
  endif()
  # MoltenVK, when the installer fetched it, is bundled next to the binary.
  if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/MoltenVK/libMoltenVK.dylib")
    add_custom_command(TARGET Haven POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:Haven>/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy
              "${CMAKE_SOURCE_DIR}/third_party/MoltenVK/libMoltenVK.dylib"
              "$<TARGET_BUNDLE_CONTENT_DIR:Haven>/Frameworks/libMoltenVK.dylib")
  endif()
endif()

# ---------------------------------------------------------------------------
#  Offscreen shader preview (developer tool, Linux/CI — needs OSMesa).
#  Renders the real shaders + real procedural meshes through a software GL
#  context so shader and geometry regressions are catchable without a Mac.
# ---------------------------------------------------------------------------
option(HAVEN_BUILD_SHADER_PREVIEW "Build the offscreen OSMesa shader preview tool" OFF)
if(HAVEN_BUILD_SHADER_PREVIEW)
  find_library(OSMESA_LIB OSMesa)
  find_library(GL_LIB GL)
  if(OSMESA_LIB AND GL_LIB)
    add_executable(haven_shader_preview "${CMAKE_SOURCE_DIR}/tools/shader_preview.cpp")
    target_link_libraries(haven_shader_preview PRIVATE haven_core ${OSMESA_LIB} ${GL_LIB})
    target_include_directories(haven_shader_preview PRIVATE "${CMAKE_SOURCE_DIR}/src")
  else()
    message(STATUS "Haven: OSMesa not found — skipping the shader preview tool")
  endif()
endif()

# ---------------------------------------------------------------------------
#  Headless tests — run everywhere, no GPU or window needed.
# ---------------------------------------------------------------------------
if(HAVEN_BUILD_TESTS)
  enable_testing()
  add_executable(haven_tests "${CMAKE_SOURCE_DIR}/tests/test_main.cpp")
  target_link_libraries(haven_tests PRIVATE haven_core)
  target_include_directories(haven_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
  add_test(NAME haven_tests COMMAND haven_tests)
endif()
