cmake_minimum_required(VERSION 3.20)
project(NukeAttributePainter VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# â”€â”€ Paths â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
if(WIN32)
    set(_NUKE_DEFAULT "C:/Program Files/Nuke17.0")
elseif(APPLE)
    set(_NUKE_DEFAULT "/Applications/Nuke17.0/Nuke17.0.app/Contents/MacOS")
else()
    set(_NUKE_DEFAULT "/usr/local/Nuke17.0")
endif()

set(NUKE_ROOT "${_NUKE_DEFAULT}" CACHE PATH "Nuke install root")
set(USD_ROOT  "${NUKE_ROOT}/usd" CACHE PATH "USD root (defaults to Nuke-bundled USD)")

set(NUKE_INCLUDE "${NUKE_ROOT}/include")
    set(NUKE_INCLUDE_INNER "${NUKE_ROOT}/include/include")
set(NUKE_LIB     "${NUKE_ROOT}")        # Windows: .lib files sit in the Nuke root
set(USD_INCLUDE  "${USD_ROOT}/include")
set(USD_LIB      "${USD_ROOT}/lib")

# â”€â”€ Sources â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
set(SOURCES
    src/plugin.cpp
    src/AttributePainterOp.cpp
    src/ViewportBrushKnob.cpp
    src/MeshSampler.cpp
    src/BrushSystem.cpp
    src/USDColorWriter.cpp
    src/UndoStack.cpp
)

add_library(AttributePainter SHARED ${SOURCES})

target_include_directories(AttributePainter PRIVATE
    include
    "${NUKE_INCLUDE}"
    "${NUKE_ROOT}/include/include"
    "${USD_INCLUDE}"
    "${USD_INCLUDE}/pxr"
    "C:/dev/tbb/include"
)

# â”€â”€ Nuke NDK libs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
find_library(DDIMAGE_LIB
    NAMES DDImage
    HINTS "${NUKE_LIB}" "${NUKE_ROOT}/lib"
    REQUIRED
)
find_library(RIKSUPPORT_LIB
    NAMES RIKsupport
    HINTS "${NUKE_LIB}" "${NUKE_ROOT}/lib"
)

# â”€â”€ USD libs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
find_library(USD_USD_LIB     usd_usd     HINTS "${USD_LIB}" REQUIRED)
find_library(USD_SDF_LIB     usd_sdf     HINTS "${USD_LIB}" REQUIRED)
find_library(USD_TF_LIB      usd_tf      HINTS "${USD_LIB}" REQUIRED)
find_library(USD_USDGEOM_LIB usd_usdGeom HINTS "${USD_LIB}" REQUIRED)
find_library(USD_VT_LIB      usd_vt      HINTS "${USD_LIB}")

# â”€â”€ OpenGL â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
find_package(OpenGL REQUIRED)

# â”€â”€ Link â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
# TBB
set(TBB_LIB "C:/Program Files/Side Effects Software/Houdini 21.0.440/custom/houdini/dsolib/tbb.lib")
target_link_libraries(AttributePainter PRIVATE
    "${DDIMAGE_LIB}"
    "${TBB_LIB}"
    "${USD_LIB}/usd_tf.lib"
    "${USD_LIB}/usd_sdf.lib"
    "${USD_LIB}/usd_vt.lib"
    "${USD_LIB}/usd_usd.lib"
    "${USD_LIB}/usd_usdGeom.lib"
    "${USD_LIB}/usd_python.lib"
    "${USD_LIB}/usd_boost.lib"
    "${USD_LIB}/usd_plug.lib"
    "${USD_LIB}/usd_arch.lib"
    "${USD_LIB}/usd_trace.lib"
    "${USD_LIB}/usd_pcp.lib"
    "C:/Users/marty/AppData/Local/Programs/Python/Python311/libs/python311.lib"
    OpenGL::GL
    OpenGL::GLU
)

# â”€â”€ Platform compile definitions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
target_compile_definitions(AttributePainter PRIVATE
    USE_GLEW
    $<$<PLATFORM_ID:Windows>:FN_OS_WINDOWS>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:_USE_MATH_DEFINES>
    $<$<PLATFORM_ID:Linux>:FN_OS_LINUX>
    $<$<PLATFORM_ID:Darwin>:FN_OS_MAC>
)

# â”€â”€ Platform compile options â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
if(MSVC)
    target_compile_options(AttributePainter PRIVATE
        /W4 /WX-
        /O2 /arch:AVX2 /fp:fast
        /MP /EHsc /permissive-
        /wd4100 /wd4251 /wd4275
    )
else()
    target_compile_options(AttributePainter PRIVATE
        -O3 -march=native -ffast-math
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

# â”€â”€ Output naming â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
set_target_properties(AttributePainter PROPERTIES
    PREFIX      ""
    OUTPUT_NAME "AttributePainter"
)
if(UNIX AND NOT APPLE)
    set_target_properties(AttributePainter PROPERTIES SUFFIX ".so")
endif()
if(MSVC)
    set_target_properties(AttributePainter PROPERTIES PDB_NAME "AttributePainter")
endif()

# â”€â”€ Install â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
install(TARGETS AttributePainter
    RUNTIME DESTINATION "${NUKE_ROOT}/plugins"
    LIBRARY DESTINATION "${NUKE_ROOT}/plugins"
)
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:AttributePainter>
        DESTINATION "${NUKE_ROOT}/plugins" OPTIONAL)
endif()

# â”€â”€ Status summary â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
message(STATUS "-------------------------------------------")
message(STATUS "  AttributePainter ${PROJECT_VERSION}")
message(STATUS "  Platform   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Nuke root  : ${NUKE_ROOT}")
message(STATUS "  USD root   : ${USD_ROOT}")
message(STATUS "  DDImage    : ${DDIMAGE_LIB}")
message(STATUS "-------------------------------------------")



