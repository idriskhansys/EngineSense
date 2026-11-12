#include <QApplication>
#include <QTimer>
#include <cstdio>
#include <cstring>

#include "MainWindow.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("EngineSense"));
  MainWindow w;
  w.show();
  w.startSimulation();

  // `--smoke-test`: used by CI under QT_QPA_PLATFORM=offscreen. Runs the live pipeline for ~2 s,
  // injects a fault and verifies the UI-facing API reports data and the fault.
  QString screenshot;
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], "--screenshot") == 0) screenshot = QString::fromLocal8Bit(argv[i + 1]);
  if (!screenshot.isEmpty()) {  // richer demo state for the README image
    QTimer::singleShot(800, [&w] { w.monitor().inject_scenario(*enginesense::find_scenario("coolant_overtemp")); });
    QTimer::singleShot(1100, [&w] { w.monitor().inject_scenario(*enginesense::find_scenario("ecu_dtc")); });
    QTimer::singleShot(2900, [&w, screenshot] { w.grab().save(screenshot); });
  }
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--smoke-test") == 0) {
      QTimer::singleShot(600, [&w] { w.monitor().inject_scenario(*enginesense::find_scenario("oil_pressure_low")); });
      QTimer::singleShot(screenshot.isEmpty() ? 2500 : 3200, [&app, &w] {
        const auto snap = w.monitor().snapshot();
        const bool ok = snap.frames_processed > 500 && snap.running;
        std::printf("smoke test: %llu frames, %zu active faults -> %s\n", static_cast<unsigned long long>(snap.frames_processed),
                    snap.active_faults.size(), ok ? "OK" : "FAILED");
        app.exit(ok ? 0 : 1);
      });
    }
  }
  return app.exec();
}
