#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

#include "pdfexport.h"
#include "scanner.h"

namespace {

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    bool isJson() const { return parser.isSet("json"); }
};

QList<ScanImage::Device> findDevices(Scanner &scanner, QString *error) {
    QList<ScanImage::Device> found;
    QEventLoop loop;
    QObject::connect(&scanner, &Scanner::devicesFound, &loop, [&](const QList<ScanImage::Device> &devices, const QString &message) {
        found = devices;
        *error = message;
        loop.quit();
    });
    scanner.discover();
    if (scanner.isDiscovering())
        loop.exec();
    return found;
}

ScanImage::Options readOptions(Scanner &scanner, const QString &device, QString *error) {
    ScanImage::Options result;
    QEventLoop loop;
    bool done = false;
    QObject::connect(&scanner, &Scanner::optionsFound, &loop, [&](const QString &, const ScanImage::Options &options, const QString &message) {
        result = options;
        *error = message;
        done = true;
        loop.quit();
    });
    scanner.queryOptions(device);
    if (!done)
        loop.exec();
    return result;
}

QJsonObject toJson(const ScanImage::Device &device) {
    return {{"id", device.id}, {"name", device.name},
            {"address", device.address.isEmpty() ? QJsonValue() : QJsonValue(device.address)}};
}

QJsonArray toJson(const QList<int> &values) {
    QJsonArray array;
    for (int value : values)
        array << value;
    return array;
}

QStringList pageSizeIds() {
    QStringList ids;
    for (const auto &size : ScanImage::pageSizes())
        ids << size.id;
    return ids;
}

int devices(const QStringList &arguments) {
    Command command("devices", "List the scanners SANE can reach. Takes a few seconds on a network.");
    command.json();
    command.parser.process(arguments);

    Scanner scanner;
    QString error;
    const auto found = findDevices(scanner, &error);
    if (found.isEmpty() && !error.isEmpty())
        return fail(error);
    if (command.isJson()) {
        QJsonArray list;
        for (const auto &device : found)
            list << toJson(device);
        print({{"devices", list}});
        return 0;
    }
    if (found.isEmpty())
        print(stdout, "No scanners found.");
    for (const auto &device : found)
        print(stdout, device.id + "\t" + device.name + (device.address.isEmpty() ? "" : "\t" + device.address));
    return 0;
}

// The scanner named on the command line, else the one the window used last,
// else the first one found. Commands read the window's memory, never write it.
bool chooseDevice(Scanner &scanner, const QString &requested, QString *device, QString *error) {
    if (!requested.isEmpty()) {
        *device = requested;
        return true;
    }
    const auto found = findDevices(scanner, error);
    if (found.isEmpty()) {
        if (error->isEmpty())
            *error = "No scanners found.";
        return false;
    }
    const QString saved = QSettings("omascan", "omascan").value("device").toString();
    *device = found.first().id;
    for (const auto &candidate : found)
        if (candidate.id == saved)
            *device = saved;
    return true;
}

int options(const QStringList &arguments) {
    Command command("options", "Show what a scanner can do: sources, modes, resolutions, and its scan area.");
    command.parser.addOption({"device", "Scanner id from omascan devices (default: the last used, else the first)", "id"});
    command.json();
    command.parser.process(arguments);

    Scanner scanner;
    QString device, error;
    if (!chooseDevice(scanner, command.parser.value("device"), &device, &error))
        return fail(error);
    const auto options = readOptions(scanner, device, &error);
    if (!error.isEmpty())
        return fail(device + ": " + error);

    if (command.isJson()) {
        print({{"device", device},
               {"sources", QJsonArray::fromStringList(options.sources)},
               {"modes", QJsonArray::fromStringList(options.modes)},
               {"resolutions", toJson(options.resolutions)},
               {"pageSizes", QJsonArray::fromStringList(pageSizeIds())},
               {"areaMillimetres", QJsonObject{{"width", options.width.max}, {"height", options.height.max}}}});
        return 0;
    }
    print(stdout, "Device:       " + device);
    print(stdout, "Sources:      " + options.sources.join(", "));
    print(stdout, "Modes:        " + options.modes.join(", "));
    QStringList resolutions;
    for (int value : options.resolutions)
        resolutions << QString::number(value);
    print(stdout, "Resolutions:  " + resolutions.join(", ") + " dpi");
    print(stdout, "Page sizes:   " + pageSizeIds().join(", "));
    print(stdout, QString("Scan area:    %1 x %2 mm").arg(options.width.max).arg(options.height.max));
    return 0;
}

int scan(const QStringList &arguments) {
    Command command("scan", "Scan into a PDF. A feeder source scans every page in it; others scan one page.");
    command.parser.addOption({"device", "Scanner id from omascan devices (default: the last used, else the first)", "id"});
    command.parser.addOption({"source", "Source, as omascan options lists it (default: the scanner's)", "source"});
    command.parser.addOption({"mode", "Color mode, as omascan options lists it (default: the scanner's)", "mode"});
    command.parser.addOption({"resolution", "Dots per inch (default: 300)", "dpi"});
    command.parser.addOption({"page", "Paper size: " + pageSizeIds().join(", ") + " (default: " + ScanImage::defaultPageSize() + ")", "size"});
    command.parser.addOption({{"o", "output"}, "The PDF to write", "file"});
    command.json();
    command.parser.process(arguments);

    const QString output = command.parser.value("output");
    if (output.isEmpty())
        return fail("Name the PDF to write with --output.");
    if (QFileInfo(output).isDir())
        return fail(output + " is a folder; name the PDF to write.");

    Scanner scanner;
    QString device, error;
    if (!chooseDevice(scanner, command.parser.value("device"), &device, &error))
        return fail(error);
    const auto options = readOptions(scanner, device, &error);
    if (!error.isEmpty())
        return fail(device + ": " + error);

    ScanImage::Settings settings;
    settings.device = device;
    settings.source = command.parser.isSet("source") ? command.parser.value("source") : options.defaultSource;
    settings.mode = command.parser.isSet("mode") ? command.parser.value("mode") : options.defaultMode;
    settings.resolution = command.parser.isSet("resolution") ? command.parser.value("resolution").toInt()
                        : options.resolutions.contains(300) ? 300 : options.defaultResolution;
    settings.page = command.parser.isSet("page") ? command.parser.value("page") : ScanImage::defaultPageSize();

    if (!options.sources.isEmpty() && !options.sources.contains(settings.source))
        return fail(QString("Unknown source \"%1\". This scanner has: %2").arg(settings.source, options.sources.join(", ")));
    if (!options.modes.isEmpty() && !options.modes.contains(settings.mode))
        return fail(QString("Unknown mode \"%1\". This scanner has: %2").arg(settings.mode, options.modes.join(", ")));
    if (!options.resolutions.isEmpty() && !options.resolutions.contains(settings.resolution)) {
        QStringList values;
        for (int value : options.resolutions)
            values << QString::number(value);
        return fail(QString("Unsupported resolution %1. This scanner has: %2").arg(command.parser.value("resolution"), values.join(", ")));
    }
    if (!pageSizeIds().contains(settings.page))
        return fail(QString("Unknown page size \"%1\". Use one of: %2").arg(settings.page, pageSizeIds().join(", ")));

    QTemporaryDir directory;
    if (!directory.isValid())
        return fail("Could not create a temporary folder: " + directory.errorString());

    QList<Page> pages;
    QString scanError;
    QEventLoop loop;
    QObject::connect(&scanner, &Scanner::pageScanned, &loop, [&](const QString &path) {
        pages << Page{path, 0, settings.resolution};
        if (!command.isJson())
            print(stderr, QString("Scanned page %1").arg(pages.size()));
    });
    QObject::connect(&scanner, &Scanner::scanFinished, &loop, [&](int, const QString &message, bool) {
        scanError = message;
        loop.quit();
    });
    scanner.scan(settings, options, directory.filePath("page-%04d.png"));
    if (scanner.isScanning())
        loop.exec();
    if (!scanError.isEmpty())
        return fail(scanError);

    if (!PdfExport::write(pages, output, &error))
        return fail(error);

    const QString path = QFileInfo(output).absoluteFilePath();
    if (command.isJson())
        print({{"output", path}, {"pages", int(pages.size())}, {"device", device}, {"source", settings.source},
               {"mode", settings.mode}, {"resolution", settings.resolution}, {"page", settings.page}});
    else
        print(stdout, QString("Wrote %1 (%2 %3)").arg(path).arg(pages.size()).arg(pages.size() == 1 ? "page" : "pages"));
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/omascan and link into ~/.claude/skills", "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    const QString action = command.parser.positionalArguments().value(1);
    if (action.isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (action != "install")
        return fail("Use omascan skill to print the skill, or omascan skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/omascan");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/omascan");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/omascan", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: omascan <command> [options]\n\n" + cliSummary());
    return 0;
}

} // namespace

QString cliSummary() {
    return "Window:\n"
           "  open                  Open the scanning window\n\n"
           "Commands that need no display (omascan help <command> for options):\n"
           "  devices               List the scanners SANE can reach\n"
           "  options               Show a scanner's sources, modes and resolutions\n"
           "  scan -o <file.pdf>    Scan into a PDF\n"
           "  skill [install]       Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"devices", "options", "scan", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "devices"   ? devices(arguments)
           : command == "options" ? options(arguments)
           : command == "scan"    ? scan(arguments)
           : command == "skill"   ? skill(arguments)
                                  : help(arguments);
}
