find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_executable(enginesense_desktop WIN32 MACOSX_BUNDLE
  main.cpp
  MainWindow.h MainWindow.cpp
  PlotWidget.h PlotWidget.cpp)
target_link_libraries(enginesense_desktop PRIVATE enginesense_core Qt6::Widgets)
enginesense_set_warnings(enginesense_desktop)
# Qt's `signals`/`slots`/`emit` keyword macros would clash with the core's SignalTable `signals`
# identifiers; Q_SIGNALS / Q_SLOTS are used instead.
target_compile_definitions(enginesense_desktop PRIVATE QT_NO_KEYWORDS)
