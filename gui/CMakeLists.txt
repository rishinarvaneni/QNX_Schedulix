cmake_minimum_required(VERSION 3.16)

project(schedulix_gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# Brake Analysis Model sources - added directly to main executable
set(BRAKE_MODEL_SRC
    src/models/BrakeModel/BrakeModel.cpp
    src/models/BrakeModel/BrakeModel.h
)

# Main executable (BrakeModel sources appended after MockProvider.cpp)
qt_add_executable(appschedulix_gui
    main.cpp
    src/app/AppController.cpp
    src/models/SystemMetrics.cpp
    src/models/TaskMetrics.cpp
    src/models/RootCauseModel.cpp
    src/models/TimelineModel.cpp
    src/models/JitterModel.cpp
    src/models/ExperimentModel.cpp
    src/models/MockProvider.cpp
    ${BRAKE_MODEL_SRC}
)

target_include_directories(appschedulix_gui PRIVATE 
    src
    src/models
    src/app
)

set_source_files_properties(qml/theme/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/theme/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/theme/Typography.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appschedulix_gui
    URI schedulix_gui
    QML_FILES
        Main.qml
        qml/theme/Colors.qml
        qml/theme/Metrics.qml
        qml/theme/Typography.qml
        qml/components/Sidebar.qml
        qml/components/SidebarIcon.qml
        qml/components/TopBar.qml
        qml/components/MetricCard.qml
        qml/components/StatusBadge.qml
        qml/screens/Dashboard.qml
        qml/screens/TimelineScreen.qml
        qml/screens/RootCauseScreen.qml
        qml/screens/ExperimentsScreen.qml
        qml/screens/JitterScreen.qml
        qml/screens/BetaScreen.qml
)

set_target_properties(appschedulix_gui PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appschedulix_gui
    PRIVATE Qt6::Quick Qt6::QuickControls2
)

install(TARGETS appschedulix_gui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
