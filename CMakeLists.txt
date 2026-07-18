cmake_minimum_required(VERSION 3.24)
project(TrezorPcMonitor VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CTest)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

add_library(pcmonitor_core
    app/src/projectmodel.cpp
    app/src/zipstore.cpp
    app/src/packcompiler.cpp
    app/src/telemetry.cpp
    app/src/presentmonprovider.cpp
    app/src/nvmlprovider.cpp
    app/src/deviceconnection.cpp
    app/src/actionexecutor.cpp
    app/src/firmwareupdater.cpp
    app/src/pixelfont.cpp
    app/src/resourceimporter.cpp
    app/src/fpsscaler.cpp
    app/src/metriccodec.cpp
    app/fonts.qrc
    protocol/protocol.c
)
target_include_directories(pcmonitor_core PUBLIC app/src protocol)
target_compile_definitions(pcmonitor_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(pcmonitor_core PUBLIC Qt6::Core Qt6::Gui)
if(WIN32)
  target_link_libraries(pcmonitor_core PUBLIC
      pdh psapi dxgi setupapi hid winusb shell32 user32 advapi32)
endif()

add_executable(trezor-pc-monitor WIN32
    app/src/main.cpp
    app/src/mainwindow.cpp
    app/src/designcanvas.cpp
    app/src/iconeditor.cpp
    app/src/resourcepreview.cpp
    app/src/xptheme.cpp
    app/src/i18n.cpp
)
if(WIN32)
  target_sources(trezor-pc-monitor PRIVATE app/windows/pcmonitor.rc)
endif()
target_link_libraries(trezor-pc-monitor PRIVATE pcmonitor_core Qt6::Widgets)

if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(pcmonitor_tests
      app/tests/pcmonitor_tests.cpp
      app/src/designcanvas.cpp
      app/src/resourcepreview.cpp
      app/tests/firmware_stubs.c
      firmware/monitor.c
  )
  target_include_directories(pcmonitor_tests PRIVATE
      app/tests firmware)
  target_compile_definitions(pcmonitor_tests PRIVATE
      EMULATOR=1 PCMONITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(pcmonitor_tests PRIVATE
      pcmonitor_core Qt6::Test Qt6::Widgets)
  add_test(NAME pcmonitor_tests COMMAND pcmonitor_tests)
  set_tests_properties(pcmonitor_tests PROPERTIES
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

set(PCMONITOR_FIRMWARE_DIR "${CMAKE_SOURCE_DIR}/firmware/prebuilt"
    CACHE PATH "Directory containing prebuilt T1B1 firmware images")

install(TARGETS trezor-pc-monitor RUNTIME DESTINATION .)
install(FILES
    "${PCMONITOR_FIRMWARE_DIR}/pcmonitor-inner.bin"
    DESTINATION firmware OPTIONAL)
