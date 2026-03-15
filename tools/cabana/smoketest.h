#pragma once

#include <string>
#include <QSize>
#include <optional>

class QWidget;

namespace cabana::smoketest {

bool enabled();
std::optional<QSize> forcedWindowSize();
std::string screenshotPath();
std::string validationStatePath();
bool sessionRestoreEnabled();
double maxUiGapMs();
int uiGapsOver16Ms();
int uiGapsOver33Ms();
int uiGapsOver50Ms();
int uiGapsOver100Ms();

void recordProcessStart();
void recordRouteLoadStart();
void recordRouteLoadDone(bool success);
void recordRouteName(const std::string &route_name);
void recordWindowShown(QWidget *window);
void recordFirstEventsMerged();
void recordFirstMsgsReceived();
void recordAutoPaused(double current_sec);
void markReady(double current_sec);
bool readyToFinalize();
bool isReady();
void noteUiTick();

}  // namespace cabana::smoketest
