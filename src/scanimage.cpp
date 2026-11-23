#include "scanimage.h"

#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <clocale>
#include <cstdint>
#include <langinfo.h>

namespace ScanImage {

namespace {

// Resolutions offered when a scanner takes any value in a range, as SANE's
// test backend does. Real scanners nearly always list their few values.
const QList<int> commonResolutions{75, 100, 150, 200, 300, 400, 600, 1200};

QString number(double value) {
    return QString::number(value, 'f', 2).remove(QRegularExpression("\\.?0+$"));
}

Range parseRange(const QString &values) {
    static const QRegularExpression range(R"(^(-?[\d.]+)\.\.(-?[\d.]+))");
    const auto match = range.match(values);
    if (!match.hasMatch())
        return {};
    return {match.captured(1).toDouble(), match.captured(2).toDouble()};
}

QList<int> parseResolutions(const QString &values) {
    QList<int> result;
    const Range range = parseRange(values);
    if (range.isValid()) {
        for (int value : commonResolutions)
            if (value >= range.min && value <= range.max)
                result << value;
        if (result.isEmpty())
            result << int(range.min) << int(range.max);
        return result;
    }
    static const QRegularExpression unit("[^\\d.]+$");
    for (QString value : values.split('|')) {
        value.remove(unit);
        const int resolution = qRound(value.toDouble());
        if (resolution > 0 && !result.contains(resolution))
            result << resolution;
    }
    return result;
}

} // namespace

namespace {
// Starts every device on a line of its own, behind a marker. Some backends
// print to standard output while they probe the network (epson2 has been
// seen dumping a printer's whole web page, with no newline at the end), so
// only marked lines are devices.
const QString deviceMarker = QStringLiteral("omascan-device|");
}

QString deviceListFormat() {
    return "%n" + deviceMarker + "%d|%v|%m|%t%n";
}

static bool isScanner(const QString &backend, const QString &type) {
    // Webcams and cameras answer as SANE devices too: v4l lists webcams as
    // "virtual device", gphoto2 cameras as "still camera". The test backend
    // is virtual as well, but it is a scanner, if a pretend one.
    static const QStringList cameraBackends{"v4l", "gphoto2", "dc210", "dc240", "dc25", "dmc", "qcam", "stv680"};
    if (cameraBackends.contains(backend))
        return false;
    static const QRegularExpression camera(R"(camera|video|webcam)", QRegularExpression::CaseInsensitiveOption);
    if (camera.match(type).hasMatch())
        return false;
    return backend == "test" || type.compare("virtual device", Qt::CaseInsensitive) != 0;
}

QList<Device> parseDevices(const QString &listing) {
    struct Entry {
        Device device;
        QString backend;
    };
    QList<Entry> entries;
    for (const QString &line : listing.split('\n', Qt::SkipEmptyParts)) {
        const QString text = line.trimmed();
        if (!text.startsWith(deviceMarker))
            continue;
        const QStringList fields = text.mid(deviceMarker.size()).split('|');
        if (fields.size() < 4 || fields[0].isEmpty())
            continue;
        const QString id = fields[0], vendor = fields[1], model = fields[2], type = fields.mid(3).join('|');
        const QString backend = id.section(':', 0, 0);
        if (!isScanner(backend, type))
            continue;

        Device device{id, {}, {}};
        if (backend == "airscan") {
            // airscan's vendor is the protocol ("eSCL", "WSD"); the model
            // already carries the maker's name.
            device.name = model;
            static const QRegularExpression ip(R"(\bip=([^\s,]+))");
            device.address = ip.match(type).captured(1);
        } else {
            device.name = vendor.isEmpty() || model.startsWith(vendor, Qt::CaseInsensitive) ? model : vendor + " " + model;
            if (backend == "escl")
                device.address = QUrl(id.section(':', 1)).host();
        }
        entries << Entry{device, backend};
    }

    // The same network scanner is found by both escl and airscan; airscan is
    // the more capable of the two, so it wins.
    QSet<QString> airscanAddresses;
    for (const auto &entry : entries)
        if (entry.backend == "airscan" && !entry.device.address.isEmpty())
            airscanAddresses << entry.device.address;

    QList<Device> devices;
    for (const auto &entry : entries)
        if (entry.backend == "airscan" || !airscanAddresses.contains(entry.device.address))
            devices << entry.device;
    return devices;
}

Options parseOptions(const QString &help) {
    Options options;
    static const QRegularExpression line(R"(^\s+(--[\w-]+|-[a-z])\s+(.+?)\s+\[([^\]]*)\]\s*$)");
    for (const QString &text : help.split('\n')) {
        const auto match = line.match(text);
        if (!match.hasMatch())
            continue;
        const QString name = match.captured(1), values = match.captured(2), current = match.captured(3);
        if (name == "--source") {
            options.sources = values.split('|');
            options.defaultSource = current;
        } else if (name == "--mode") {
            options.modes = values.split('|');
            options.defaultMode = current;
        } else if (name == "--resolution") {
            options.resolutions = parseResolutions(values);
            options.defaultResolution = qRound(current.toDouble());
        } else if (name == "-x") {
            options.width = parseRange(values);
        } else if (name == "-y") {
            options.height = parseRange(values);
        }
    }
    return options;
}

bool isFeeder(const QString &source) {
    static const QRegularExpression feeder(R"(\bADF\b|feeder)", QRegularExpression::CaseInsensitiveOption);
    return feeder.match(source).hasMatch();
}

QStringList scanArguments(const Settings &settings, const Options &options, const QString &pattern) {
    QStringList arguments{"-d", settings.device};
    if (!settings.source.isEmpty() && options.sources.contains(settings.source))
        arguments << "--source" << settings.source;
    if (!settings.mode.isEmpty() && options.modes.contains(settings.mode))
        arguments << "--mode" << settings.mode;
    if (settings.resolution > 0)
        arguments << "--resolution" << QString::number(settings.resolution);

    // Ask for the paper's area from the top-left corner, trimmed to what the
    // scanner reaches; scanimage would round it anyway, with a warning.
    const PageSize page = pageSize(settings.page);
    if (page.width > 0 && options.width.isValid() && options.height.isValid()) {
        arguments << "-l" << "0" << "-t" << "0"
                  << "-x" << number(std::min(page.width, options.width.max))
                  << "-y" << number(std::min(page.height, options.height.max));
    }

    arguments << "--format=png" << "--batch=" + pattern << "--batch-print" << "--progress";
    if (!isFeeder(settings.source))
        arguments << "--batch-count=1";
    return arguments;
}

QList<PageSize> pageSizes() {
    return {
        {"a4", 210, 297},
        {"letter", 215.9, 279.4},
        {"legal", 215.9, 355.6},
        {"a5", 148, 210},
        {"a3", 297, 420},
        {"full", 0, 0},
    };
}

PageSize pageSize(const QString &id) {
    for (const auto &size : pageSizes())
        if (size.id == id)
            return size;
    return pageSize(defaultPageSize());
}

// The paper the system's LC_PAPER names, which is what printers on this
// machine assume too: US Letter is 216 mm wide, everything else is A4 here.
QString defaultPageSize() {
#ifdef _NL_PAPER_WIDTH
    std::setlocale(LC_PAPER, "");
    const auto width = reinterpret_cast<intptr_t>(nl_langinfo(_NL_PAPER_WIDTH));
    if (width > 0)
        return width == 216 ? "letter" : "a4";
#endif
    return QLocale().measurementSystem() == QLocale::ImperialUSSystem ? "letter" : "a4";
}

double parseProgress(const QString &text) {
    static const QRegularExpression progress(R"(Progress:\s*([\d.]+)%)");
    double last = -1;
    auto it = progress.globalMatch(text);
    while (it.hasNext())
        last = it.next().captured(1).toDouble();
    return last;
}

QString errorMessage(const QString &stderrText) {
    QString last;
    for (QString line : stderrText.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
        if (!line.startsWith("scanimage: "))
            continue;
        line = line.mid(11).trimmed();
        // Rounding a value to what the scanner supports is not a failure.
        if (line.startsWith("rounded value"))
            continue;
        if (line.startsWith("sane_start: ") || line.startsWith("sane_read: "))
            line = line.section(": ", 1);
        last = line;
    }
    return last;
}

} // namespace ScanImage
