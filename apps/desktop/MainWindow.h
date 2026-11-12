#pragma once
#include <QMainWindow>
#include <QSet>
#include <QTimer>
#include <map>

#include "enginesense/monitor.hpp"

class PlotWidget;
class QComboBox;
class QLabel;
class QProgressBar;
class QTableWidget;

/// Live monitoring window. The UI thread only ever calls Monitor::snapshot()/history()/
/// drain_notifications() from a 30 Hz timer; all decoding and detection runs on worker threads.
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  enginesense::Monitor& monitor() { return monitor_; }
  void startSimulation();

 private Q_SLOTS:
  void onTick();
  void openLog();
  void stop();
  void injectSelected();
  void exportFaults();

 private:
  double selectedSpeed() const;
  void appendFault(const enginesense::FaultNotification& n);
  void updatePlot();

  enginesense::Monitor monitor_;
  QTimer timer_;
  QTableWidget* signalTable_;
  QTableWidget* faultTable_;
  QProgressBar* busLoad_;
  PlotWidget* plot_;
  QComboBox* plotSignal_;
  QComboBox* scenarioBox_;
  QComboBox* speedBox_;
  QLabel* statusLabel_;
  std::map<std::uint64_t, int> faultRowById_;
};
