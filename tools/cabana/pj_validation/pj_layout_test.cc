#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <iostream>

#include "tools/cabana/pj_layout/layout_model.h"

using cabana::pj_layout::ComputeLayoutStats;
using cabana::pj_layout::LayoutModel;
using cabana::pj_layout::LayoutStats;
using cabana::pj_layout::LoadLayoutFile;
using cabana::pj_layout::ParseLayoutXml;
using cabana::pj_layout::ToXmlString;

namespace {

QStringList JsonStringList(const QJsonArray &array) {
  QStringList values;
  values.reserve(array.size());
  for (const auto &value : array) {
    values.push_back(value.toString());
  }
  return values;
}

void PrintFailure(const QString &layout_name, const QString &message) {
  std::cerr << layout_name.toStdString() << ": " << message.toStdString() << std::endl;
}

bool CompareStats(const QString &layout_name, const QJsonObject &expected, const LayoutStats &actual) {
  bool ok = true;
  auto check_int = [&](const char *key, int value) {
    const int expected_value = expected.value(key).toInt();
    if (expected_value != value) {
      PrintFailure(layout_name,
                   QString("%1 expected %2 got %3").arg(key).arg(expected_value).arg(value));
      ok = false;
    }
  };

  check_int("tabs", actual.tabs);
  check_int("plots", actual.plots);
  check_int("splitters", actual.splitters);
  check_int("xy_plots", actual.xy_plots);
  check_int("timeseries_plots", actual.timeseries_plots);
  check_int("custom_math_snippets", actual.custom_math_snippets);

  const bool expected_reactive = expected.value("reactive_script_editor").toBool();
  if (expected_reactive != actual.reactive_script_editor) {
    PrintFailure(layout_name,
                 QString("reactive_script_editor expected %1 got %2")
                     .arg(expected_reactive)
                     .arg(actual.reactive_script_editor));
    ok = false;
  }

  const QStringList expected_transforms = JsonStringList(expected.value("transforms").toArray());
  if (expected_transforms != actual.transforms) {
    PrintFailure(layout_name,
                 QString("transforms expected [%1] got [%2]")
                     .arg(expected_transforms.join(", "))
                     .arg(actual.transforms.join(", ")));
    ok = false;
  }

  return ok;
}

}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);

  QString contract_path = "tools/cabana/pj_validation/layout_contract.json";
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == "--contract" && i + 1 < args.size()) {
      contract_path = args[++i];
    }
  }

  QFile contract_file(contract_path);
  if (!contract_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    std::cerr << "failed to open contract: " << contract_path.toStdString() << std::endl;
    return 2;
  }

  const QJsonDocument contract = QJsonDocument::fromJson(contract_file.readAll());
  const QJsonObject root = contract.object();
  const QString layout_dir = root.value("layout_dir").toString();
  const QJsonArray layouts = root.value("layouts").toArray();

  bool ok = true;
  for (const auto &layout_value : layouts) {
    const QJsonObject expected = layout_value.toObject();
    const QString layout_name = expected.value("layout").toString();
    const QString layout_path = layout_dir + "/" + layout_name;

    LayoutModel model;
    QString error;
    if (!LoadLayoutFile(layout_path, &model, &error)) {
      PrintFailure(layout_name, "parse failed: " + error);
      ok = false;
      continue;
    }

    if (!CompareStats(layout_name, expected, ComputeLayoutStats(model))) {
      ok = false;
    }

    LayoutModel reparsed;
    if (!ParseLayoutXml(ToXmlString(model, 1), &reparsed, &error)) {
      PrintFailure(layout_name, "round-trip parse failed: " + error);
      ok = false;
      continue;
    }
    if (!(model == reparsed)) {
      PrintFailure(layout_name, "round-trip model mismatch");
      ok = false;
      continue;
    }

    std::cout << layout_name.toStdString() << ": ok" << std::endl;
  }

  return ok ? 0 : 1;
}
