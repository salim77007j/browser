qt_add_executable(kestrel WIN32
  src/main.cpp
  src/app.h src/app.cpp
  src/icons.h src/icons.cpp
  src/theme.h src/theme.cpp
  src/omnibox.h src/omnibox.cpp
  src/tabstrip.h src/tabstrip.cpp
  src/web_page.h src/web_page.cpp
  src/web_view.h src/web_view.cpp
  src/request_interceptor.h src/request_interceptor.cpp
  src/scheme_handler.h src/scheme_handler.cpp
  src/internal_pages.h src/internal_pages.cpp
  src/bridge.h src/bridge.cpp
  src/downloads.h src/downloads.cpp
  src/browser_window.h src/browser_window.cpp
)

# Pure-Widgets app: no QML, skip qmlimportscanner (works without root too)
set_target_properties(kestrel PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)

target_include_directories(kestrel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kestrel PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
  Qt6::WebEngineCore Qt6::WebEngineWidgets Qt6::Network
  kestrel_core_iface
)

qt_add_resources(kestrel "res"
  PREFIX "/"
  FILES
    resources/pages/internal.css
    resources/pages/qwebchannel.js
    resources/filters/easylist.txt
    resources/filters/easyprivacy.txt
    resources/filters/annoyances.txt
)
