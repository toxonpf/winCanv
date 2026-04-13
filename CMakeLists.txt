cmake_minimum_required(VERSION 3.24)
project(workspace-templates VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui DBus Network)

qt_standard_project_setup()
include(CTest)

qt_add_executable(workspace-templates
    src/main.cpp
    src/app_paths.cpp
    src/models.cpp
    src/template_store.cpp
    src/desktop_entries.cpp
    src/kwin_controller.cpp
    src/workspace_manager.cpp
    src/control_service.cpp
    src/workspace_panel.cpp
)

target_include_directories(workspace-templates PRIVATE src)

target_link_libraries(workspace-templates PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
)

install(TARGETS workspace-templates RUNTIME DESTINATION bin)
install(FILES workspace_templates/assets/io.github.toxonpf.workspacetemplates.desktop
    DESTINATION share/applications
)
install(DIRECTORY workspace_templates/assets/kwin-shortcut/
    DESTINATION share/kwin/scripts/workspacetemplates-shortcut
)

if(BUILD_TESTING)
    add_test(NAME workspace-templates-help COMMAND workspace-templates --help)
    add_test(NAME workspace-templates-version COMMAND workspace-templates --version)
    add_test(NAME workspace-templates-list COMMAND workspace-templates list)
endif()
