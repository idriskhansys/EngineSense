#include "MainWindow.h"

#include <QAction>
#include <QBrush>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <fstream>

#include "PlotWidget.h"
#include "enginesense/log_io.hpp"

using namespace enginesense;

namespace {
const char* statusText(SignalStatus s) { return to_string(s); }

QColor severityColor(Severity s) {
  switch (s) {
    case Severity::Critical: return QColor(0xF8, 0xD0, 0xD0);
    case Severity::Warning: return QColor(0xFC, 0xEB, 0xC2);
    case Severity::Info: return QColor(0xDD, 0xE9, 0xF8);
  }
  return QColor();
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("EngineSense - J1939 engine diagnostics"));
  resize(1180, 760);

  auto* tb = addToolBar(QStringLiteral("Controls"));
  tb->setMovable(false);
  tb->addAction(QStringLiteral("Start simulation"), this, &MainWindow::startSimulation);
  tb->addAction(QStringLiteral("Open log..."), this, &MainWindow::openLog);
  tb->addAction(QStringLiteral("Stop"), this, &MainWindow::stop);
  tb->addSeparator();
  tb->addWidget(new QLabel(QStringLiteral(" Speed: ")));
  speedBox_ = new QComboBox;
  speedBox_->addItems({QStringLiteral("1x"), QStringLiteral("5x"), QStringLiteral("20x"), QStringLiteral("Max")});
  speedBox_->setCurrentIndex(2);
  tb->addWidget(speedBox_);
  tb->addSeparator();
  tb->addWidget(new QLabel(QStringLiteral(" Inject fault: ")));
  scenarioBox_ = new QComboBox;
  for (const auto& s : standard_scenarios())
    scenarioBox_->addItem(QString::fromStdString(s.name), QString::fromStdString(s.description));
  tb->addWidget(scenarioBox_);
  auto* injectBtn = new QPushButton(QStringLiteral("Inject"));
  tb->addWidget(injectBtn);
  connect(injectBtn, &QPushButton::clicked, this, &MainWindow::injectSelected);
  connect(scenarioBox_, &QComboBox::currentIndexChanged, this,
          [this](int i) { scenarioBox_->setToolTip(scenarioBox_->itemData(i).toString()); });
  tb->addSeparator();
  tb->addAction(QStringLiteral("Export faults..."), this, &MainWindow::exportFaults);

  signalTable_ = new QTableWidget(static_cast<int>(kSignalCount), 3);
  signalTable_->setHorizontalHeaderLabels({QStringLiteral("Signal"), QStringLiteral("Value"), QStringLiteral("Status")});
  signalTable_->verticalHeader()->hide();
  signalTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  signalTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  signalTable_->horizontalHeader()->setStretchLastSection(true);
  plotSignal_ = new QComboBox;
  for (std::size_t i = 0; i < kSignalCount; ++i) {
    const SignalDef& d = signal_def(static_cast<SignalId>(i));
    signalTable_->setItem(static_cast<int>(i), 0, new QTableWidgetItem(QString::fromUtf8(d.name)));
    signalTable_->setItem(static_cast<int>(i), 1, new QTableWidgetItem(QStringLiteral("-")));
    signalTable_->setItem(static_cast<int>(i), 2, new QTableWidgetItem(QStringLiteral("unknown")));
    plotSignal_->addItem(QString::fromUtf8(d.name));
  }
  plotSignal_->setCurrentIndex(static_cast<int>(index_of(SignalId::CoolantTemp)));
  signalTable_->setColumnWidth(0, 120);
  signalTable_->setColumnWidth(1, 120);
  signalTable_->setMinimumWidth(340);

  busLoad_ = new QProgressBar;
  busLoad_->setRange(0, 100);
  busLoad_->setFormat(QStringLiteral("CAN bus load %p %"));

  plot_ = new PlotWidget;

  auto* left = new QWidget;
  auto* lv = new QVBoxLayout(left);
  lv->addWidget(new QLabel(QStringLiteral("<b>Live signals</b>")));
  lv->addWidget(signalTable_, 1);
  lv->addWidget(busLoad_);
  auto* right = new QWidget;
  auto* rv = new QVBoxLayout(right);
  auto* ph = new QHBoxLayout;
  ph->addWidget(new QLabel(QStringLiteral("<b>Trend:</b>")));
  ph->addWidget(plotSignal_);
  ph->addStretch(1);
  rv->addLayout(ph);
  rv->addWidget(plot_, 1);

  auto* top = new QSplitter(Qt::Horizontal);
  top->addWidget(left);
  top->addWidget(right);
  top->setStretchFactor(1, 1);

  faultTable_ = new QTableWidget(0, 6);
  faultTable_->setHorizontalHeaderLabels({QStringLiteral("Time [s]"), QStringLiteral("State"), QStringLiteral("Severity"),
                                          QStringLiteral("Type"), QStringLiteral("Signal"), QStringLiteral("Message")});
  faultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  faultTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  faultTable_->horizontalHeader()->setStretchLastSection(true);
  faultTable_->verticalHeader()->hide();

  auto* central = new QSplitter(Qt::Vertical);
  central->addWidget(top);
  auto* faultBox = new QWidget;
  auto* fv = new QVBoxLayout(faultBox);
  fv->addWidget(new QLabel(QStringLiteral("<b>Fault log</b>")));
  fv->addWidget(faultTable_);
  central->addWidget(faultBox);
  central->setStretchFactor(0, 3);
  central->setStretchFactor(1, 2);
  setCentralWidget(central);

  statusLabel_ = new QLabel(QStringLiteral("idle"));
  statusBar()->addWidget(statusLabel_, 1);

  connect(&timer_, &QTimer::timeout, this, &MainWindow::onTick);
  timer_.start(33);
}

MainWindow::~MainWindow() { monitor_.stop(); }

double MainWindow::selectedSpeed() const {
  switch (speedBox_->currentIndex()) {
    case 0: return 1.0;
    case 1: return 5.0;
    case 2: return 20.0;
    default: return 0.0;
  }
}

void MainWindow::startSimulation() {
  faultTable_->setRowCount(0);
  faultRowById_.clear();
  Monitor::Options o;
  o.speed = selectedSpeed();
  if (o.speed == 0.0) o.duration_us = 600ull * 1'000'000;  // an unthrottled endless run would only burn CPU
  monitor_.start_simulation(SimConfig{}, o);
}

void MainWindow::openLog() {
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open CAN log"), QString(),
                                                    QStringLiteral("candump logs (*.log *.txt);;All files (*)"));
  if (path.isEmpty()) return;
  LogReadResult log;
  if (!read_log_file(path.toStdString(), log) || log.frames.empty()) {
    QMessageBox::warning(this, QStringLiteral("EngineSense"), QStringLiteral("Could not read any CAN frames from\n%1").arg(path));
    return;
  }
  if (log.bad_lines > 0)
    statusBar()->showMessage(QStringLiteral("%1 malformed lines skipped (first: line %2)").arg(log.bad_lines).arg(log.first_bad_line), 5000);
  faultTable_->setRowCount(0);
  faultRowById_.clear();
  Monitor::Options o;
  o.speed = selectedSpeed();
  monitor_.start_replay(std::move(log.frames), o);
}

void MainWindow::stop() { monitor_.stop(); }

void MainWindow::injectSelected() {
  const int i = scenarioBox_->currentIndex();
  if (i >= 0 && monitor_.running()) monitor_.inject_scenario(standard_scenarios()[static_cast<std::size_t>(i)]);
  else statusBar()->showMessage(QStringLiteral("Start a simulation first - faults can only be injected into a running simulation"), 4000);
}

void MainWindow::exportFaults() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export fault log"), QStringLiteral("faults.csv"),
                                                    QStringLiteral("CSV (*.csv)"));
  if (path.isEmpty()) return;
  std::ofstream os(path.toStdString());
  write_fault_csv(os, monitor_.all_notifications());
}

void MainWindow::appendFault(const FaultNotification& n) {
  const FaultEvent& e = n.event;
  const bool raised = n.kind == FaultNotification::Kind::Raised;
  if (raised) {
    const int row = faultTable_->rowCount();
    faultTable_->insertRow(row);
    const QStringList cells = {QString::number(static_cast<double>(e.raised_us) / 1e6, 'f', 3), QStringLiteral("ACTIVE"),
                               QString::fromUtf8(to_string(e.severity)), QString::fromUtf8(to_string(e.type)),
                               e.signal ? QString::fromUtf8(signal_def(*e.signal).name) : QString(),
                               QString::fromStdString(e.message)};
    for (int c = 0; c < cells.size(); ++c) {
      auto* item = new QTableWidgetItem(cells[c]);
      item->setBackground(QBrush(severityColor(e.severity)));
      item->setForeground(QBrush(Qt::black));
      faultTable_->setItem(row, c, item);
    }
    faultRowById_[e.id] = row;
    faultTable_->scrollToBottom();
  } else {
    const auto it = faultRowById_.find(e.id);
    if (it == faultRowById_.end()) return;
    auto* state = faultTable_->item(it->second, 1);
    if (state) state->setText(QStringLiteral("cleared %1 s").arg(static_cast<double>(e.cleared_us) / 1e6, 0, 'f', 1));
    for (int c = 0; c < faultTable_->columnCount(); ++c)
      if (auto* item = faultTable_->item(it->second, c)) item->setBackground(QBrush());
  }
}

void MainWindow::updatePlot() {
  const auto id = static_cast<SignalId>(plotSignal_->currentIndex());
  const SignalDef& d = signal_def(id);
  const auto hist = monitor_.history(id, 600);
  QVector<PlotWidget::Sample> v;
  v.reserve(static_cast<int>(hist.size()));
  for (const auto& s : hist) v.push_back({s.t_s, s.value});
  plot_->setSamples(v, QString::fromUtf8(d.name), QString::fromUtf8(d.unit));
  const RuleConfig rc;
  double limit = std::nan("");
  if (id == SignalId::CoolantTemp) limit = rc.coolant_over_c;
  else if (id == SignalId::OilTemp) limit = rc.oil_temp_over_c;
  else if (id == SignalId::OilPressure) limit = rc.oil_pressure_low_kpa;
  else if (id == SignalId::EngineSpeed) limit = rc.overspeed_rpm;
  plot_->setLimit(limit);
}

void MainWindow::onTick() {
  const MonitorSnapshot s = monitor_.snapshot();  // short lock, never waits for detection work
  for (std::size_t i = 0; i < kSignalCount; ++i) {
    const SignalValue& sv = s.signals[i];
    const SignalDef& d = signal_def(static_cast<SignalId>(i));
    signalTable_->item(static_cast<int>(i), 1)->setText(
        sv.status == SignalStatus::Valid ? QStringLiteral("%1 %2").arg(sv.value, 0, 'f', 2).arg(QString::fromUtf8(d.unit)) : QStringLiteral("-"));
    signalTable_->item(static_cast<int>(i), 2)->setText(QString::fromUtf8(statusText(sv.status)));
  }
  busLoad_->setValue(static_cast<int>(std::min(100.0, s.bus_load * 100.0)));
  for (const auto& n : monitor_.drain_notifications()) appendFault(n);
  updatePlot();
  statusLabel_->setText(QStringLiteral("%1 | t = %2 s | %3 frames | %4 dropped | %5 active faults")
                            .arg(s.running ? QStringLiteral("RUNNING") : QStringLiteral("stopped"))
                            .arg(static_cast<double>(s.time_us) / 1e6, 0, 'f', 1)
                            .arg(s.frames_processed)
                            .arg(s.frames_dropped)
                            .arg(s.active_faults.size()));
}
