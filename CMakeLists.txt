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

qt_add_executable(dsh-berth
    WIN32
    assets/berth.rc
    src/main.cpp
    src/Instance.h
    src/InstanceModel.h
    src/InstanceModel.cpp
    src/Settings.h
    src/Settings.cpp
    src/Supervisor.h
    src/Supervisor.cpp
    src/AppController.h
    src/AppController.cpp
    src/PluginOps.h
    src/PluginOps.cpp
    src/ProfileOps.h
    src/ProfileOps.cpp
    src/ProfileTreeModel.h
    src/ProfileTreeModel.cpp
    src/DshUpdate.h
    src/DshUpdate.cpp
)

qt_add_resources(dsh-berth "berth_icons"
    PREFIX "/"
    FILES assets/berth-logo.png
)

qt_add_qml_module(dsh-berth
    URI Berth
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/InstancePane.qml
        qml/SettingsPane.qml
        qml/PluginsDialog.qml
        qml/ProfileDialog.qml
        qml/WebWindow.qml
)

target_include_directories(dsh-berth PRIVATE src)

target_link_libraries(dsh-berth PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Widgets
    Qt6::WebView
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
