#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <cstdlib>
#include <cstdio>

#include "tools/cabana/mainwin.h"
#include "tools/cabana/pjwindow.h"
#include "tools/cabana/streams/devicestream.h"
#include "tools/cabana/streams/pandastream.h"
#include "tools/cabana/streams/replaystream.h"
#ifdef __linux__
#include "tools/cabana/streams/socketcanstream.h"
#endif

int main(int argc, char *argv[]) {
  const QString startup_cwd = QDir::currentPath();
  QCoreApplication::setApplicationName("Cabana");
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  initApp(argc, argv, false);

#ifdef __linux__
  if (!std::getenv("QT_QPA_PLATFORM") && !std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY")) {
    std::fprintf(stderr, "cabana requires a GUI display, but neither DISPLAY nor WAYLAND_DISPLAY is set.\n");
    std::fprintf(stderr, "Run it from a desktop session, or set up X/Wayland forwarding before starting cabana.\n");
    return 1;
  }
#endif

  QApplication app(argc, argv);
  app.setApplicationDisplayName("Cabana");
  app.setWindowIcon(QIcon(":cabana-icon.png"));

  UnixSignalHandler signalHandler;
  utils::setTheme(settings.theme);

  QCommandLineParser cmd_parser;
  cmd_parser.addHelpOption();
  cmd_parser.addPositionalArgument("route", "the drive to replay. find your drives at connect.comma.ai");
  cmd_parser.addOption({"demo", "use a demo route instead of providing your own"});
  cmd_parser.addOption({"auto", "Auto load the route from the best available source (no video): internal, openpilotci, comma_api, car_segments, testing_closet"});
  cmd_parser.addOption({"qcam", "load qcamera"});
  cmd_parser.addOption({"ecam", "load wide road camera"});
  cmd_parser.addOption({"dcam", "load driver camera"});
  cmd_parser.addOption({"msgq", "read can messages from the msgq"});
  cmd_parser.addOption({"panda", "read can messages from panda"});
  cmd_parser.addOption({"panda-serial", "read can messages from panda with given serial", "panda-serial"});
#ifdef __linux__
  if (SocketCanStream::available()) {
    cmd_parser.addOption({"socketcan", "read can messages from given SocketCAN device", "socketcan"});
  }
#endif
  cmd_parser.addOption({"zmq", "read can messages from zmq at the specified ip-address", "ip-address"});
  cmd_parser.addOption({"data_dir", "local directory with routes", "data_dir"});
  cmd_parser.addOption({"no-vipc", "do not output video"});
  cmd_parser.addOption({"dbc", "dbc file to open", "dbc"});
  cmd_parser.addOption({"pj", "launch PlotJuggler mode inside Cabana"});
  cmd_parser.addOption({"pj-layout", "PlotJuggler layout file to load", "pj-layout"});
  cmd_parser.process(app);

  AbstractStream *stream = nullptr;
  const bool pj_mode = cmd_parser.isSet("pj");

  if (pj_mode && (cmd_parser.isSet("msgq") || cmd_parser.isSet("zmq") || cmd_parser.isSet("panda") ||
                  cmd_parser.isSet("panda-serial")
#ifdef __linux__
                  || cmd_parser.isSet("socketcan")
#endif
                  )) {
    std::fprintf(stderr, "cabana --pj currently supports replay routes only; live inputs like --msgq, --zmq, --panda, and --socketcan are not supported.\n");
    return 1;
  }

  if (!pj_mode && cmd_parser.isSet("msgq")) {
    stream = new DeviceStream(&app);
  } else if (!pj_mode && cmd_parser.isSet("zmq")) {
    stream = new DeviceStream(&app, cmd_parser.value("zmq"));
  } else if (!pj_mode && (cmd_parser.isSet("panda") || cmd_parser.isSet("panda-serial"))) {
    try {
      stream = new PandaStream(&app, {.serial = cmd_parser.value("panda-serial").toStdString()});
    } catch (std::exception &e) {
      qWarning() << e.what();
      return 0;
    }
#ifdef __linux__
  } else if (!pj_mode && SocketCanStream::available() && cmd_parser.isSet("socketcan")) {
    stream = new SocketCanStream(&app, {.device = cmd_parser.value("socketcan").toStdString()});
#endif
  } else {
    uint32_t replay_flags = REPLAY_FLAG_NONE;
    if (cmd_parser.isSet("ecam")) replay_flags |= REPLAY_FLAG_ECAM;
    if (cmd_parser.isSet("qcam")) replay_flags |= REPLAY_FLAG_QCAMERA;
    if (cmd_parser.isSet("dcam")) replay_flags |= REPLAY_FLAG_DCAM;
    if (cmd_parser.isSet("no-vipc")) replay_flags |= REPLAY_FLAG_NO_VIPC;

    const QStringList args = cmd_parser.positionalArguments();
    QString route;
    if (args.size() > 0) {
      route = args.first();
    } else if (cmd_parser.isSet("demo")) {
      route = DEMO_ROUTE;
    }
    if (!route.isEmpty()) {
      auto replay_stream = std::make_unique<ReplayStream>(&app);
      bool auto_source = cmd_parser.isSet("auto");
      if (!replay_stream->loadRoute(route.toStdString(), cmd_parser.value("data_dir").toStdString(), replay_flags, auto_source, pj_mode)) {
        return 0;
      }
      stream = replay_stream.release();
    }
  }

  if (pj_mode) {
    if (!stream) {
      std::fprintf(stderr, "cabana --pj requires a replay route or --demo.\n");
      return 1;
    }
    QString layout_file = cmd_parser.value("pj-layout");
    if (!layout_file.isEmpty()) {
      QFileInfo layout_info(layout_file);
      layout_file = layout_info.isAbsolute() ? layout_info.absoluteFilePath() : QDir(startup_cwd).absoluteFilePath(layout_file);
    }
    PlotJugglerWindow w(stream, cmd_parser.value("dbc"), layout_file);
    return app.exec();
  }

  MainWindow w(stream, cmd_parser.value("dbc"));
  return app.exec();
}
