#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// Everything omascan knows about SANE's scanimage command: what it prints and
// what it takes. Kept free of processes so the parsing can be tested on
// captured output from real scanners.
namespace ScanImage {

struct Device {
    QString id;      // what scanimage -d takes, e.g. "airscan:e1:Brother MFC-J6955DW"
    QString name;    // what people read
    QString address; // the scanner's IP address, when the backend says
};

struct Range {
    double min = 0;
    double max = 0;
    bool isValid() const { return max > min; }
};

struct Options {
    QStringList sources;
    QStringList modes;
    QList<int> resolutions;
    QString defaultSource;
    QString defaultMode;
    int defaultResolution = 0;
    Range width;  // millimetres the scanner can reach across
    Range height; // and down
};

// Paper sizes in millimetres. "full" has no size and scans everything the
// scanner can reach.
struct PageSize {
    QString id;
    double width = 0;
    double height = 0;
};

struct Settings {
    QString device;
    QString source;
    QString mode;
    int resolution = 0;
    QString page;
};

// Parses `scanimage -f` with deviceListFormat(): only lines carrying its
// marker are read, so whatever else a backend prints is ignored. Webcams and
// cameras are dropped, and a scanner that both the escl and airscan backends
// found is listed once, as airscan.
QList<Device> parseDevices(const QString &listing);
QString deviceListFormat();

// The same scanner in a fresh list, or -1. airscan numbers its devices in the
// order one discovery happens to find them ("airscan:e1:Name"), so an id kept
// from an earlier discovery may no longer open; the scanner is then found by
// the name in its id, else by its shown name.
int findDevice(const QList<Device> &devices, const QString &id, const QString &name = {});

// Parses `scanimage -d <device> -A`.
Options parseOptions(const QString &help);

// The arguments for one scan into numbered PNG files named by `pattern`
// (a printf pattern such as "/dir/scan-1-%04d.png"). A feeder scans until it
// is empty; anything else scans one page.
QStringList scanArguments(const Settings &settings, const Options &options, const QString &pattern);

bool isFeeder(const QString &source);

QList<PageSize> pageSizes();
PageSize pageSize(const QString &id);
QString defaultPageSize();

// The last "Progress: 42.1%" in a chunk of stderr, or -1.
double parseProgress(const QString &text);

// The last real error scanimage printed, without its "scanimage: " prefix.
QString errorMessage(const QString &stderrText);

// Whether an errorMessage() says the device could not be opened, as happens
// to an airscan id that a newer discovery has numbered differently.
bool isOpenFailure(const QString &message);

} // namespace ScanImage

Q_DECLARE_METATYPE(ScanImage::Device)
Q_DECLARE_METATYPE(ScanImage::Options)
