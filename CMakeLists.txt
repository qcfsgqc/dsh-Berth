cmake_minimum_required(VERSION 3.21)

project(dsh-Berth
    VERSION 0.1.0
    DESCRIPTION "DSH Berth — unofficial Windows supervisor for DeepSeek Harness"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "dsh-Berth currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(BERTH_BUILD_TESTS "Build the berth_tests unit/property test executable" ON)

find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Network
    Widgets
    WebView
)

qt_standard_project_setup(REQUIRES 6.10.2)

# ---------------------------------------------------------------------------
# berth_core: pure logic shared by dsh-berth and berth_tests.
# Links Qt6::Core only; must not include Gui/Qml/Widgets/windows.h.
# ---------------------------------------------------------------------------
file(GLOB BERTH_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h
)

qt_add_library(berth_core STATIC
    ${BERTH_CORE_SOURCES}
)

# Headers are included as "core/Xxx.h".
target_include_directories(berth_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(berth_core PUBLIC
    Qt6::Core
)

qt_add_executable(dsh-berth
    WIN32
    assets/berth.rc
    src/main.cpp
    src/Instance.h
    src/InstanceModel.h
    src/InstanceModel.cpp
    src/Settings.h
    src/Settings.cpp
    src/DataStore.h
    src/DataStore.cpp
    src/Supervisor.h
    src/Supervisor.cpp
    src/ProcessMetrics.h
    src/ProcessMetrics.cpp
    src/PortChecker.h
    src/PortChecker.cpp
    src/ProcessRunner.h
    src/ProcessRunner.cpp
    src/ProxyManager.h
    src/ProxyManager.cpp
    src/AppController.h
    src/AppController.cpp
    src/AutoRestarter.h
    src/AutoRestarter.cpp
    src/AutostartRegistry.h
    src/AutostartRegistry.cpp
    src/BatchLauncher.h
    src/BatchLauncher.cpp
    src/LogService.h
    src/LogService.cpp
    src/TrayController.h
    src/TrayController.cpp
    src/Notifier.h
    src/Notifier.cpp
    src/PluginOps.h
    src/PluginOps.cpp
    src/PluginManager.h
    src/PluginManager.cpp
    src/RestartHintController.h
    src/RestartHintController.cpp
    src/ExtensionManager.h
    src/ExtensionManager.cpp
    src/ProfileOps.h
    src/ProfileOps.cpp
    src/ProfileTreeModel.h
    src/ProfileTreeModel.cpp
    src/DshUpdate.h
    src/DshUpdate.cpp
    src/DshVersionStore.h
    src/DshVersionStore.cpp
    src/Preflight.h
    src/Preflight.cpp
    src/Quarantine.h
    src/Quarantine.cpp
    src/BundleIO.h
    src/BundleIO.cpp
    src/StatsService.h
    src/StatsService.cpp
    src/WorkspaceOpener.h
    src/WorkspaceOpener.cpp
    src/EnvChecker.h
    src/EnvChecker.cpp
    src/UpdateCenter.h
    src/UpdateCenter.cpp
)

qt_add_resources(dsh-berth "berth_icons"
    PREFIX "/"
    FILES assets/berth-logo.png
)

# Node helper scripts, extracted to <dataDir>/helpers/ on first use
qt_add_resources(dsh-berth "berth_helpers"
    PREFIX "/"
    FILES assets/helpers/usage-scan.mjs
)

qt_add_qml_module(dsh-berth
    URI Berth
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/InstancePane.qml
        qml/EnvArgsEditor.qml
        qml/BerthIcon.qml
        qml/InstanceMetaEditor.qml
        qml/GroupedInstanceList.qml
        qml/SummaryPanel.qml
        qml/SettingsPane.qml
        qml/PluginsDialog.qml
        qml/MarketPage.qml
        qml/ProfileDialog.qml
        qml/PortConflictDialog.qml
        qml/LogViewer.qml
        qml/WebWindow.qml
        qml/RestartHintBar.qml
        qml/VersionsPage.qml
        qml/ExtensionsPage.qml
        qml/BundleDialogs.qml
        qml/UsagePage.qml
        qml/ModelPriceEditor.qml
        qml/EnvPage.qml
        qml/ProxySettings.qml
        qml/MetricsPanel.qml
        qml/UpdatePage.qml
)

target_include_directories(dsh-berth PRIVATE src)

target_link_libraries(dsh-berth PRIVATE
    berth_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
    Qt6::WebView
    iphlpapi
    crypt32
)

target_compile_definitions(dsh-berth PRIVATE
    BERTH_VERSION="${PROJECT_VERSION}"
    BERTH_APP_NAME="DSH Berth"
)

set_target_properties(dsh-berth PROPERTIES
    OUTPUT_NAME "dsh-berth"
    WIN32_EXECUTABLE TRUE
)

install(TARGETS dsh-berth RUNTIME DESTINATION .)

# ---------------------------------------------------------------------------
# berth_tests: QtTest runner for berth_core (no Widgets/Qml).
# Each tests/tst_*.cpp self-registers via BERTH_TEST(Class); main.cpp never
# needs editing when a new test file is added.
# ---------------------------------------------------------------------------
if(BERTH_BUILD_TESTS)
    find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS Test)

    enable_testing()

    file(GLOB BERTH_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tst_*.cpp
    )

    qt_add_executable(berth_tests
        tests/main.cpp
        tests/BerthTest.h
        tests/gen/Gen.h
        ${BERTH_TEST_SOURCES}
    )

    target_include_directories(berth_tests PRIVATE tests)

    target_link_libraries(berth_tests PRIVATE
        berth_core
        Qt6::Core
        Qt6::Test
    )

    add_test(NAME berth_tests COMMAND berth_tests)
endif()
