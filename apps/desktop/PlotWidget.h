#pragma once
#include <QString>
#include <QVector>
#include <QWidget>

/// Minimal dependency-free time-series plot (QPainter only, so the app needs just Qt Widgets).
class PlotWidget : public QWidget {
  Q_OBJECT
 public:
  explicit PlotWidget(QWidget* parent = nullptr);

  struct Sample {
    double t;
    double v;
  };
  void setSamples(const QVector<Sample>& samples, const QString& title, const QString& unit);
  /// Optional horizontal limit marker (NaN disables).
  void setLimit(double value);

 protected:
  void paintEvent(QPaintEvent* event) override;
  QSize sizeHint() const override { return {520, 260}; }

 private:
  QVector<Sample> samples_;
  QString title_, unit_;
  double limit_;
};
