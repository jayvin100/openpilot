#pragma once

#include "core/types.h"

namespace cabana {
namespace file_dialogs {

void requestOpenDbc();
void requestOpenDbc(const SourceSet &sources);
void requestSaveDbcAs();
void requestSaveDbcAs(int source);
void requestExportCsv();
void render();

}  // namespace file_dialogs
}  // namespace cabana
