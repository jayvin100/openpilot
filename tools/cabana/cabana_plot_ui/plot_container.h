#pragma once

#include "tools/cabana/cabana_plot_ui/pj_chart.h"

namespace cabana::plot_ui {

/// PlotContainer is now a thin wrapper around PjChartView.
/// Kept for API compatibility with PlotTabWidget.
using PlotContainer = PjChartView;

}  // namespace cabana::plot_ui
