// omascan — scan paper to PDF from a SANE scanner.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "translator.h"

// The desktop's interface font, e.g. "Adwaita Sans 11", read from the
// settings portal as Hype does, since the generic platform theme won't.
static void adoptDesktopFont() {
    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.Settings", "ReadOne");
    call.setArguments({"org.gnome.desktop.interface", "font-name"});
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;
    const QString name = reply.arguments().first().value<QDBusVariant>().variant().toString();
    const int space = name.lastIndexOf(' ');
    const double size = name.mid(space + 1).toDouble();
    if (space <= 0 || size <= 0)
        return;
    QFont font(name.left(space));
    font.setPointSizeF(size);
    QGuiApplication::setFont(font);
}

int main(int argc, char *argv[]) {
    // omascan themes itself; Qt's gtk3 platform theme only adds GTK's own
    // bugs when the desktop theme changes under a running window.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");

    // Bare omascan prints help, as a command line tool should; the launcher
    // says omascan open. Commands draw no window, so they need no display.
    const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool window = first == "open";
    if (!window)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("omascan");
    app.setOrganizationName("omascan");
    app.setApplicationVersion("0.1.3");

    if (first == "--version" || first == "-v") {
        printf("omascan %s\n", qPrintable(app.applicationVersion()));
        return 0;
    }
    if (!window) {
        if (!first.isEmpty() && !isCliCommand(first) && first != "--help" && first != "-h") {
            fprintf(stderr, "Unknown command: %s\n\n", qPrintable(first));
            fprintf(stderr, "Usage: omascan <command> [options]\n\n%s\n", qPrintable(cliSummary()));
            return 1;
        }
        return runCli(app.arguments());
    }

    // Matches the window to omascan.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("omascan");
    app.setWindowIcon(QIcon::fromTheme("omascan"));
    adoptDesktopFont();

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
