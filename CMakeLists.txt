cmake_minimum_required(VERSION 3.21)
project(KestrelBrowser VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg WebEngineCore WebEngineWidgets Network)
qt_standard_project_setup()

add_subdirectory(core)
add_subdirectory(app)
