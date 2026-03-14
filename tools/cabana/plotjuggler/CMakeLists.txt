cmake_minimum_required(VERSION 3.16)

project(cabana_plotjuggler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(OPENPILOT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(CABANA_PJ_OUTPUT_DIR "${CABANA_PJ_OUTPUT_DIR}" CACHE PATH "Directory for PlotJuggler runtime outputs")
if(NOT CABANA_PJ_OUTPUT_DIR)
  set(CABANA_PJ_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CABANA_PJ_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CABANA_PJ_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

add_definitions(
  -DPJ_MAJOR_VERSION=3
  -DPJ_MINOR_VERSION=6
  -DPJ_PATCH_VERSION=1
  -DFMT_HEADER_ONLY
  -DDYNAMIC_CAPNP
)

find_package(Qt5 REQUIRED COMPONENTS
  Core
  Gui
  Widgets
  PrintSupport
  Concurrent
  Xml
  Svg
  OpenGL
  Network
  WebSockets)
find_package(BZip2 REQUIRED)
find_package(Threads REQUIRED)

if(UNIX AND NOT APPLE)
  find_package(Qt5 REQUIRED COMPONENTS X11Extras)
endif()

find_path(CAPNP_INCLUDE_DIR capnp/schema-parser.h REQUIRED)
find_library(CAPNP_LIB capnp REQUIRED)
find_library(CAPNPC_LIB capnpc REQUIRED)
find_library(KJ_LIB kj REQUIRED)
find_library(ZMQ_LIB zmq REQUIRED)
find_library(SSL_LIB ssl REQUIRED)
find_library(CRYPTO_LIB crypto REQUIRED)
find_library(XCB_LIB xcb)

set(OPENPILOT_COMMON_LIB "${OPENPILOT_ROOT}/common/libcommon.a")
set(OPENPILOT_CEREAL_LIB "${OPENPILOT_ROOT}/cereal/libcereal.a")
set(OPENPILOT_MSGQ_LIB "${OPENPILOT_ROOT}/msgq_repo/libmsgq.a")
set(OPENPILOT_JSON11_LIB "${OPENPILOT_ROOT}/third_party/libjson11.a")

foreach(required_file
    "${OPENPILOT_COMMON_LIB}"
    "${OPENPILOT_CEREAL_LIB}"
    "${OPENPILOT_MSGQ_LIB}"
    "${OPENPILOT_JSON11_LIB}")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "Missing required openpilot static library: ${required_file}")
  endif()
endforeach()

set(QT_LIBS
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::PrintSupport
  Qt5::Concurrent
  Qt5::Xml
  Qt5::Svg
  Qt5::OpenGL
  Qt5::Network
  Qt5::WebSockets)

if(UNIX AND NOT APPLE)
  list(APPEND QT_LIBS Qt5::X11Extras)
endif()

include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/QCodeEditor/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Qt-Advanced-Docking/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Qt-Advanced-Docking/include/Qads"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/color_widgets/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lua-5.4.3/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nlohmann"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/qwt/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/sol"
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_base/src"
  "${OPENPILOT_ROOT}"
  "${OPENPILOT_ROOT}/msgq"
  "${OPENPILOT_ROOT}/cereal"
  "${OPENPILOT_ROOT}/cereal/gen/cpp"
  "${CAPNP_INCLUDE_DIR}")

file(GLOB QWT_SOURCES
  3rdparty/qwt/src/*.cpp
  3rdparty/qwt/src/*.h)
list(FILTER QWT_SOURCES EXCLUDE REGEX ".*/qwt_plot_glcanvas\\.(cpp|h)$")
add_library(plotjuggler_qwt STATIC ${QWT_SOURCES})
target_compile_definitions(plotjuggler_qwt PUBLIC QWT_MOC_INCLUDE)
target_link_libraries(plotjuggler_qwt PUBLIC Qt5::Widgets Qt5::Concurrent Qt5::Svg Qt5::PrintSupport Qt5::OpenGL)

add_library(colorwidgets STATIC
  3rdparty/color_widgets/include/color_names.hpp
  3rdparty/color_widgets/include/color_palette.hpp
  3rdparty/color_widgets/include/color_preview.hpp
  3rdparty/color_widgets/include/color_utils.hpp
  3rdparty/color_widgets/include/color_wheel.hpp
  3rdparty/color_widgets/include/swatch.hpp
  3rdparty/color_widgets/src/color_names.cpp
  3rdparty/color_widgets/src/color_palette.cpp
  3rdparty/color_widgets/src/color_preview.cpp
  3rdparty/color_widgets/src/color_utils.cpp
  3rdparty/color_widgets/src/color_wheel.cpp
  3rdparty/color_widgets/src/swatch.cpp
  3rdparty/color_widgets/src/color_widgets.qrc)
target_include_directories(colorwidgets PUBLIC 3rdparty/color_widgets/include)
target_link_libraries(colorwidgets PUBLIC Qt5::Widgets)

file(GLOB QCODEEDITOR_SOURCES
  3rdparty/QCodeEditor/src/internal/*.cpp
  3rdparty/QCodeEditor/include/*
  3rdparty/QCodeEditor/include/internal/*)
add_library(QCodeEditor STATIC
  ${QCODEEDITOR_SOURCES}
  3rdparty/QCodeEditor/resources/qcodeeditor_resources.qrc)
target_include_directories(QCodeEditor PUBLIC 3rdparty/QCodeEditor/include)
target_link_libraries(QCodeEditor PUBLIC Qt5::Core Qt5::Widgets Qt5::Gui)

file(GLOB ADS_SOURCES
  3rdparty/Qt-Advanced-Docking/src/*.cpp
  3rdparty/Qt-Advanced-Docking/include/Qads/*.h)
if(UNIX AND NOT APPLE)
  list(APPEND ADS_SOURCES 3rdparty/Qt-Advanced-Docking/src/linux/FloatingWidgetTitleBar.cpp)
endif()
add_library(qt_advanced_docking STATIC
  ${ADS_SOURCES}
  3rdparty/Qt-Advanced-Docking/src/ads.qrc)
target_include_directories(qt_advanced_docking PUBLIC 3rdparty/Qt-Advanced-Docking/include)
target_compile_definitions(qt_advanced_docking PUBLIC ADS_STATIC)
target_link_libraries(qt_advanced_docking PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
if(UNIX AND NOT APPLE)
  target_link_libraries(qt_advanced_docking PUBLIC Qt5::X11Extras)
  if(XCB_LIB)
    target_link_libraries(qt_advanced_docking PRIVATE "${XCB_LIB}")
  endif()
endif()

file(GLOB LUA_SOURCES 3rdparty/lua-5.4.3/src/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/lua\\.c$")
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/luac\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC 3rdparty/lua-5.4.3/src)
if(NOT WIN32)
  target_compile_definitions(lua_static PUBLIC LUA_USE_POSIX)
endif()

file(GLOB PLOTJUGGLER_BASE_SOURCES
  plotjuggler_base/src/*.cpp
  plotjuggler_base/include/PlotJuggler/*.h
  plotjuggler_base/include/PlotJuggler/util/*.hpp)
add_library(plotjuggler_base STATIC ${PLOTJUGGLER_BASE_SOURCES})
target_include_directories(plotjuggler_base PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_base/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_base/src")
target_link_libraries(plotjuggler_base PRIVATE lua_static plotjuggler_qwt Qt5::Widgets Qt5::Xml)

add_library(plotjuggler_openpilot_common STATIC
  plotjuggler_plugins/DataLoadRlog/rlog_parser.cpp
  plotjuggler_plugins/DataLoadRlog/rlog_parser.hpp)
target_include_directories(plotjuggler_openpilot_common PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_plugins"
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_plugins/DataLoadRlog")
target_link_libraries(plotjuggler_openpilot_common PUBLIC
  Qt5::Widgets
  Qt5::Xml
  plotjuggler_base
  "${CAPNP_LIB}"
  "${CAPNPC_LIB}"
  "${KJ_LIB}")

file(GLOB_RECURSE PLOTJUGGLER_APP_SOURCES
  plotjuggler_app/*.cpp
  plotjuggler_app/*.h
  plotjuggler_app/*.ui)
list(FILTER PLOTJUGGLER_APP_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(plotjuggler_app_core STATIC
  ${PLOTJUGGLER_APP_SOURCES}
  plotjuggler_app/resource.qrc)
target_include_directories(plotjuggler_app_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_app")
target_compile_definitions(plotjuggler_app_core PRIVATE MainWindow=PlotJugglerMainWindow)
target_link_libraries(plotjuggler_app_core PUBLIC
  ${QT_LIBS}
  colorwidgets
  lua_static
  qt_advanced_docking
  plotjuggler_base
  plotjuggler_qwt
  QCodeEditor)

add_library(cabana_plotjuggler SHARED
  cabana_plotjuggler_widget.cpp
  cabana_plotjuggler_widget.h)
target_include_directories(cabana_plotjuggler PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/plotjuggler_app_core_autogen/include")
target_compile_definitions(cabana_plotjuggler PRIVATE MainWindow=PlotJugglerMainWindow)
target_link_libraries(cabana_plotjuggler PRIVATE
  ${QT_LIBS}
  plotjuggler_app_core
  plotjuggler_openpilot_common)
if(UNIX AND NOT APPLE)
  target_link_options(cabana_plotjuggler PRIVATE "-Wl,-Bsymbolic-functions")
endif()

add_library(DataLoadRlog SHARED
  plotjuggler_plugins/DataLoadRlog/dataload_rlog.cpp
  plotjuggler_plugins/DataLoadRlog/dataload_rlog.hpp)
target_link_libraries(DataLoadRlog PRIVATE
  Qt5::Widgets
  Qt5::Xml
  plotjuggler_openpilot_common
  plotjuggler_base
  BZip2::BZip2)

add_library(DataStreamCereal SHARED
  plotjuggler_plugins/DataStreamCereal/datastream_cereal.cpp
  plotjuggler_plugins/DataStreamCereal/datastream_cereal.h
  plotjuggler_plugins/DataStreamCereal/datastream_cereal.ui)
target_include_directories(DataStreamCereal PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/plotjuggler_plugins")
target_link_libraries(DataStreamCereal PRIVATE
  Qt5::Widgets
  plotjuggler_openpilot_common
  plotjuggler_base
  "${OPENPILOT_COMMON_LIB}"
  "${OPENPILOT_CEREAL_LIB}"
  "${OPENPILOT_MSGQ_LIB}"
  "${OPENPILOT_JSON11_LIB}"
  "${ZMQ_LIB}"
  "${SSL_LIB}"
  "${CRYPTO_LIB}"
  Threads::Threads)

add_library(ToolboxLuaEditor SHARED
  plotjuggler_plugins/ToolboxLuaEditor/lua_editor.cpp
  plotjuggler_plugins/ToolboxLuaEditor/lua_editor.h
  plotjuggler_plugins/ToolboxLuaEditor/lua_editor.ui)
target_link_libraries(ToolboxLuaEditor PRIVATE
  Qt5::Widgets
  Qt5::Xml
  plotjuggler_base
  plotjuggler_qwt
  lua_static
  QCodeEditor)
