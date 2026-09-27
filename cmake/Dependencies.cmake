# Third-party dependencies. Every download is pinned by SHA-256 and cached in
# MAUSELY_DEPS_DIR so repeated configures work offline.

set(MAUSELY_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH "Download cache for third-party files")
file(MAKE_DIRECTORY "${MAUSELY_DEPS_DIR}")

# Download `url` to `dest` unless a file with the expected hash is already there.
function(mausely_download url dest sha256)
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" have)
    if(have STREQUAL sha256)
      return()
    endif()
    message(STATUS "Hash mismatch for ${dest}, downloading again")
    file(REMOVE "${dest}")
  endif()
  get_filename_component(name "${dest}" NAME)
  message(STATUS "Downloading ${name}")
  # Unique temporary name: the Android build configures one tree per ABI in parallel.
  string(MD5 tag "${PROJECT_BINARY_DIR}")
  set(part "${dest}.${tag}.part")
  file(DOWNLOAD "${url}" "${part}" EXPECTED_HASH SHA256=${sha256} STATUS st TLS_VERIFY ON)
  list(GET st 0 code)
  if(NOT code EQUAL 0)
    file(REMOVE "${part}")
    message(FATAL_ERROR "Download failed: ${url}\n${st}")
  endif()
  file(RENAME "${part}" "${dest}")
endfunction()

# Download and extract a source archive whose single top-level folder is `dirname`.
function(mausely_fetch_archive url sha256 dirname out_var)
  get_filename_component(name "${url}" NAME)
  set(archive "${MAUSELY_DEPS_DIR}/${dirname}.tar.gz")
  mausely_download("${url}" "${archive}" ${sha256})
  set(root "${PROJECT_BINARY_DIR}/_deps")
  if(NOT EXISTS "${root}/${dirname}/.extracted-${sha256}")
    file(REMOVE_RECURSE "${root}/${dirname}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${root}")
    file(TOUCH "${root}/${dirname}/.extracted-${sha256}")
  endif()
  set(${out_var} "${root}/${dirname}" PARENT_SCOPE)
endfunction()

if(WIN32)  # the dashboard exists on Windows only
  # --- Dear ImGui (MIT) -----------------------------------------------------
  mausely_fetch_archive(
    "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9.tar.gz"
    af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
    imgui-1.92.9 IMGUI_DIR)
  add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
  target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi imm32)

  # --- ImPlot (MIT) ---------------------------------------------------------
  mausely_fetch_archive(
    "https://github.com/epezent/implot/archive/refs/tags/v1.0.tar.gz"
    e4a9db64eef7bcc604e2a5ea380af124eb97aa3e8a6a96330079c88add0f7e93
    implot-1.0 IMPLOT_DIR)
  add_library(implot STATIC ${IMPLOT_DIR}/implot.cpp ${IMPLOT_DIR}/implot_items.cpp)
  target_include_directories(implot SYSTEM PUBLIC ${IMPLOT_DIR})
  target_link_libraries(implot PUBLIC imgui)
  # ImPlot 1.0 still uses ImGui APIs that IMGUI_DISABLE_OBSOLETE_FUNCTIONS removes,
  # so that flag stays off. Third-party warnings are not ours to fix.
  if(NOT MSVC)
    target_compile_options(imgui PRIVATE -w)
    target_compile_options(implot PRIVATE -w)
  endif()
endif()

# --- stb_image (public domain / MIT) ----------------------------------------
set(STB_DIR "${MAUSELY_DEPS_DIR}/stb")
mausely_download(
  "https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h"
  "${STB_DIR}/stb_image.h"
  594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${STB_DIR})

# --- doctest (MIT) ----------------------------------------------------------
if(MAUSELY_BUILD_TESTS)
  set(DOCTEST_DIR "${MAUSELY_DEPS_DIR}/doctest")
  mausely_download(
    "https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h"
    "${DOCTEST_DIR}/doctest/doctest.h"
    cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421)
  add_library(doctest INTERFACE)
  target_include_directories(doctest SYSTEM INTERFACE ${DOCTEST_DIR})
endif()

# --- ONNX Runtime 1.30.0 (MIT) ----------------------------------------------
# Only the C API header and the shared library are needed: the library is
# loaded at runtime (LoadLibrary / dlopen), so any compiler (MSVC, MinGW,
# clang) can build us. The library is taken from the official PyPI wheel
# (14 MB instead of the 83 MB zip). Android gets it from the onnxruntime-android
# AAR instead (see android/app/build.gradle.kts).
set(ORT_VERSION 1.30.0)
set(ORT_INCLUDE_DIR "${MAUSELY_DEPS_DIR}/onnxruntime-${ORT_VERSION}/include")
set(ORT_HEADER_URL "https://raw.githubusercontent.com/microsoft/onnxruntime/v${ORT_VERSION}/include/onnxruntime/core/session")
mausely_download("${ORT_HEADER_URL}/onnxruntime_c_api.h" "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h"
  e035e30c27e74c8c00e0f483e576e12b4067d17f12e9237fd6eff8b346c9b381)
mausely_download("${ORT_HEADER_URL}/onnxruntime_ep_c_api.h" "${ORT_INCLUDE_DIR}/onnxruntime_ep_c_api.h"
  e6c986c9e98583f8113b2c6bc3864814883b806d501cf24da4d239c45753e235)
mausely_download("${ORT_HEADER_URL}/onnxruntime_error_code.h" "${ORT_INCLUDE_DIR}/onnxruntime_error_code.h"
  5ce3b054e798eced8d14f5b86e98692fd33470463f96194ce0700a2d53dd8721)
add_library(onnxruntime_headers INTERFACE)
target_include_directories(onnxruntime_headers SYSTEM INTERFACE ${ORT_INCLUDE_DIR})

# Extracts `member` from the wheel at `url` into the deps folder as `out`.
function(mausely_ort_from_wheel url sha256 member out)
  set(lib "${MAUSELY_DEPS_DIR}/onnxruntime-${ORT_VERSION}/${out}")
  if(NOT EXISTS "${lib}")
    get_filename_component(wheel_name "${url}" NAME)
    set(wheel "${MAUSELY_DEPS_DIR}/${wheel_name}")
    mausely_download("${url}" "${wheel}" ${sha256})
    set(tmp "${PROJECT_BINARY_DIR}/_deps/ort-wheel")
    file(ARCHIVE_EXTRACT INPUT "${wheel}" DESTINATION "${tmp}" PATTERNS "${member}")
    file(MAKE_DIRECTORY "${MAUSELY_DEPS_DIR}/onnxruntime-${ORT_VERSION}")
    file(COPY_FILE "${tmp}/${member}" "${lib}")
  endif()
  set(ORT_LIBRARY "${lib}" PARENT_SCOPE)
endfunction()

set(ORT_PYPI "https://files.pythonhosted.org/packages")
if(ANDROID)
  # Packaged by Gradle from the AAR; nothing to download here.
elseif(WIN32)
  set(ORT_LIBRARY_NAME onnxruntime.dll)
  mausely_ort_from_wheel(
    "${ORT_PYPI}/a6/13/0f1699f6de549c9324bc9112a2a85b14c517904cd11b562a654643b755a1/onnxruntime-${ORT_VERSION}-cp312-cp312-win_amd64.whl"
    f3501472571f1b1eee50e017851e7929f5ea37312d2d8c2494a19e8fc58b4a38
    "onnxruntime/capi/onnxruntime.dll" onnxruntime.dll)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(ORT_LIBRARY_NAME libonnxruntime.so)
  mausely_ort_from_wheel(
    "${ORT_PYPI}/34/35/e7f862dbacbc99fadd9b14a614e49c99bf0f35fd9927a82f096e3de33531/onnxruntime-${ORT_VERSION}-cp312-cp312-manylinux_2_28_x86_64.whl"
    fa688e7891a6aa206636fe7372e27ee75fd17713289f6b4fc7b190e0a7de9328
    "onnxruntime/capi/libonnxruntime.so.${ORT_VERSION}" libonnxruntime.so)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(ORT_LIBRARY_NAME libonnxruntime.so)
  mausely_ort_from_wheel(
    "${ORT_PYPI}/16/bd/cbc5b8f91963689fdd622f463508c01d0aa95d3f944747b1e0b1eb2160b8/onnxruntime-${ORT_VERSION}-cp312-cp312-manylinux_2_28_aarch64.whl"
    6c32a000d5139a38ba9349030b0032e3331acb559d596b22738d9d2b343a2b83
    "onnxruntime/capi/libonnxruntime.so.${ORT_VERSION}" libonnxruntime.so)
else()
  message(FATAL_ERROR "No prebuilt ONNX Runtime is configured for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()
