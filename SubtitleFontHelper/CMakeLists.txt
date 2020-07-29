
cmake_minimum_required (VERSION 3.8)

project ("SubtitleFontHelper")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS REQUIRED Core Gui Widgets)

find_path(FREETYPE_INCLUDE_DIR freetype/freetype.h)
find_library(FREETYPE_LIBRARY freetype.lib)

set(project_sources "MainWindow.cpp" "EntryPoint.cpp" "FontDatabase.cpp" "ASSReader.cpp"  "CommandInterface.cpp" "Common.cpp" "ConfigFile.cpp" "Daemon.cpp" "ProcessMonitor.cpp" "Logger.cpp")
set(project_headers "MainWindow.h" "FontDatabase.h" "ASSReader.h"  "CommandInterface.h" "Common.h" "ConfigFile.h" "Daemon.h" "ProcessMonitor.h" "Logger.h")
set(project_resources "resources.qrc")
set(project_ui "MainWindow.ui")
set(WMI_LIBS "wbemuuid.lib")

qt5_wrap_ui(ui_wrap ${project_ui})
qt5_wrap_cpp(moc_sources ${project_headers})

if(CMAKE_BUILD_TYPE MATCHES "Debug")
set(QUAZIP_LIBS "${CMAKE_SOURCE_DIR}/quazip/quazip/debug/quazipd.lib" "${CMAKE_SOURCE_DIR}/zlib/build/Debug/zlibstaticd.lib")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")

if(CMAKE_BUILD_TYPE MATCHES "Release")
set(QUAZIP_LIBS "${CMAKE_SOURCE_DIR}/quazip/quazip/release/quazip.lib" "${CMAKE_SOURCE_DIR}/zlib/build/Release/zlibstatic.lib")
endif(CMAKE_BUILD_TYPE MATCHES "Release")

if(CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")
set(QUAZIP_LIBS "${CMAKE_SOURCE_DIR}/quazip/quazip/release/quazip.lib" "${CMAKE_SOURCE_DIR}/zlib/build/Release/zlibstatic.lib")
endif(CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")

add_executable(SubtitleFontHelper WIN32
	${project_sources} 
	${project_headers}
	${ui_wrap} 
	${moc_sources}
	${project_resources}
)

target_include_directories(SubtitleFontHelper PRIVATE 
	${FREETYPE_INCLUDE_DIR}
	${CMAKE_SOURCE_DIR}/quazip/quazip
	${CMAKE_SOURCE_DIR}/zlib
	${CMAKE_SOURCE_DIR}/zlib/build
	Qt5::Core 
	Qt5::Gui
	Qt5::Widgets 
)

target_link_libraries(SubtitleFontHelper PRIVATE 
	${FREETYPE_LIBRARY}
	${QUAZIP_LIBS}
	${WMI_LIBS}
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

if(WIN32)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Core> $<TARGET_FILE_DIR:${PROJECT_NAME}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Gui> $<TARGET_FILE_DIR:${PROJECT_NAME}>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Widgets> $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
endif(WIN32)

if(WIN32)
	#set_target_properties(SubtitleFontHelper PROPERTIES LINK_FLAGS "/ENTRY:wWinMainCRTStartup")
endif(WIN32)
