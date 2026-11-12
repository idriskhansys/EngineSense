#include "PlotWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent), limit_(std::numeric_limits<double>::quiet_NaN()) {
  setMinimumHeight(180);
}

void PlotWidget::setSamples(const QVector<Sample>& samples, const QString& title, const QString& unit) {
  samples_ = samples;
  title_ = title;
  unit_ = unit;
  update();
}

void PlotWidget::setLimit(double value) {
  limit_ = value;
  update();
}

void PlotWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), palette().base());
  const QRectF plot(56, 26, width() - 72.0, height() - 52.0);
  p.setPen(palette().color(QPalette::Text));
  p.drawText(QPointF(8, 16), title_.isEmpty() ? QStringLiteral("(no data)") : title_ + QStringLiteral(" [") + unit_ + QStringLiteral("]"));
  p.setPen(QPen(palette().color(QPalette::Mid), 1));
  p.drawRect(plot);
  if (samples_.size() < 2) return;

  double tmin = samples_.first().t, tmax = samples_.last().t;
  double vmin = samples_.first().v, vmax = vmin;
  for (const Sample& s : samples_) {
    vmin = std::min(vmin, s.v);
    vmax = std::max(vmax, s.v);
  }
  if (!std::isnan(limit_)) {
    vmin = std::min(vmin, limit_);
    vmax = std::max(vmax, limit_);
  }
  if (vmax - vmin < 1e-9) {
    vmax += 1.0;
    vmin -= 1.0;
  }
  const double pad = (vmax - vmin) * 0.08;
  vmin -= pad;
  vmax += pad;
  if (tmax - tmin < 1e-9) tmax = tmin + 1.0;

  auto map = [&](double t, double v) {
    return QPointF(plot.left() + (t - tmin) / (tmax - tmin) * plot.width(),
                   plot.bottom() - (v - vmin) / (vmax - vmin) * plot.height());
  };

  p.setPen(palette().color(QPalette::Text));
  for (int i = 0; i <= 4; ++i) {  // y axis labels
    const double v = vmin + (vmax - vmin) * i / 4.0;
    const QPointF pt = map(tmin, v);
    p.drawText(QRectF(0, pt.y() - 8, 52, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', 1));
    p.setPen(QPen(palette().color(QPalette::Midlight), 1));
    p.drawLine(QPointF(plot.left(), pt.y()), QPointF(plot.right(), pt.y()));
    p.setPen(palette().color(QPalette::Text));
  }
  p.drawText(QRectF(plot.left(), plot.bottom() + 4, 80, 16), Qt::AlignLeft, QString::number(tmin, 'f', 0) + QStringLiteral(" s"));
  p.drawText(QRectF(plot.right() - 80, plot.bottom() + 4, 80, 16), Qt::AlignRight, QString::number(tmax, 'f', 0) + QStringLiteral(" s"));

  if (!std::isnan(limit_)) {
    p.setPen(QPen(QColor(0xEB, 0x68, 0x34), 1.5, Qt::DashLine));
    p.drawLine(map(tmin, limit_), map(tmax, limit_));
  }
  QPainterPath path;
  path.moveTo(map(samples_.first().t, samples_.first().v));
  for (const Sample& s : samples_) path.lineTo(map(s.t, s.v));
  p.setPen(QPen(QColor(0x2A, 0x78, 0xD6), 2));
  p.drawPath(path);
}
