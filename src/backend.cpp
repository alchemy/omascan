#include "backend.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrent>

#include "filepicker.h"
#include "pdfexport.h"
#include "portalfilepicker.h"
#include "scanner.h"

Backend::Backend(Scanner *scanner, FilePicker *picker, QObject *parent)
    : QObject(parent),
      m_scanner(scanner ? scanner : new Scanner({}, this)),
      m_picker(picker ? picker : new PortalFilePicker(this)),
      m_store("omascan", "omascan") {
    // Scans stay in the cache, not /tmp, which is memory on Omarchy and a
    // long feeder run at 600 dpi is hundreds of megabytes.
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/omascan";
    QDir().mkpath(cache);
    removeStaleSessions(cache);
    m_session = std::make_unique<QTemporaryDir>(cache + "/session-XXXXXX");

    m_settings.source = m_store.value("source").toString();
    m_settings.mode = m_store.value("mode").toString();
    m_settings.resolution = m_store.value("resolution").toInt();
    m_settings.page = m_store.value("page", ScanImage::defaultPageSize()).toString();

    connect(m_scanner, &Scanner::devicesFound, this, [this](const QList<ScanImage::Device> &devices, const QString &error) {
        const QString current = m_deviceIndex >= 0 ? m_devices[m_deviceIndex].id : QString();
        const QString currentName = m_deviceIndex >= 0 ? m_devices[m_deviceIndex].name : QString();
        m_devices = devices;
        emit devicesChanged();
        emit busyChanged();
        if (!error.isEmpty() && devices.isEmpty())
            setError(error);

        // Stay on the scanner in use; else the one used last time; else the
        // first.
        int index = ScanImage::findDevice(devices, current, currentName);
        if (index < 0)
            index = ScanImage::findDevice(devices, m_store.value("device").toString(), m_store.value("deviceName").toString());
        if (index < 0 && !devices.isEmpty())
            index = 0;

        // A scan waiting on this discovery goes ahead only on its own scanner.
        if (!m_retryScanner.isEmpty() && (index < 0 || devices[index].name != m_retryScanner)) {
            setError(tr("%1 is no longer on the network.").arg(m_retryScanner));
            m_retryScanner.clear();
        }
        m_deviceIndex = -2; // forces the options to be read again
        setDeviceIndex(index);
    });

    connect(m_scanner, &Scanner::optionsFound, this, [this](const QString &device, const ScanImage::Options &options, const QString &error) {
        if (m_deviceIndex < 0 || device != m_devices[m_deviceIndex].id)
            return;
        if (!error.isEmpty())
            setError(tr("Could not read the settings of %1: %2").arg(m_devices[m_deviceIndex].name, error));
        applyOptions(options);
        emit busyChanged();
        if (!m_retryScanner.isEmpty() && m_devices[m_deviceIndex].name == m_retryScanner) {
            m_retryScanner.clear();
            if (error.isEmpty())
                startScan();
        }
    });

    connect(m_scanner, &Scanner::pageScanned, this, [this](const QString &path) {
        // One undo takes back the whole scan, recorded at its first page so
        // a scan that brings nothing leaves no empty step behind.
        if (m_scannedPages == 0)
            m_pages.checkpoint();
        ++m_scannedPages;
        m_progress = 0;
        emit progressChanged();
        m_pages.append(Page{path, 0, m_settings.resolution});
        emit pageAdded(m_pages.count() - 1);
    });
    connect(m_scanner, &Scanner::progress, this, [this](double percent) {
        m_progress = percent;
        emit progressChanged();
    });
    connect(m_scanner, &Scanner::scanFinished, this, [this](int pages, const QString &error, bool cancelled) {
        emit busyChanged();
        // An airscan id goes stale when the scanners on the network answer
        // in another order than when they were found. Find them again, and
        // scan once more on the same scanner under its new id.
        if (pages == 0 && !cancelled && !m_retried && m_deviceIndex >= 0 && ScanImage::isOpenFailure(error)) {
            m_retried = true;
            m_retryScanner = m_devices[m_deviceIndex].name;
            discover();
            return;
        }
        if (!error.isEmpty())
            setError(error);
    });

    connect(&m_pages, &PageModel::changed, this, &Backend::updateDirty);
    connect(m_picker, &FilePicker::saveSelected, this, &Backend::exportTo);
    connect(m_picker, &FilePicker::failed, this, &Backend::setError);
    connect(&m_export, &QFutureWatcher<QString>::finished, this, [this] {
        const QString error = m_export.result();
        emit busyChanged();
        if (!error.isEmpty()) {
            setError(error);
            return;
        }
        m_exportedRevision = m_exportRevision;
        m_store.setValue("exportFolder", QFileInfo(m_exportPath).absolutePath());
        updateDirty();
        emit exported(m_exportPath);
    });

    if (const QString missing = m_scanner->missingMessage(); !missing.isEmpty())
        setError(missing);
    else
        discover();
}

Backend::~Backend() {
    m_export.waitForFinished();
}

// A session directory outlives the app only when it crashed; anything a day
// old is not in use.
void Backend::removeStaleSessions(const QString &cache) {
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    for (const QFileInfo &entry : QDir(cache).entryInfoList({"session-*"}, QDir::Dirs | QDir::NoDotAndDotDot))
        if (entry.lastModified() < cutoff)
            QDir(entry.absoluteFilePath()).removeRecursively();
}

QVariantList Backend::devices() const {
    QVariantList list;
    for (const auto &device : m_devices)
        list << QVariantMap{{"id", device.id}, {"name", device.name}, {"address", device.address}};
    return list;
}

QVariantList Backend::resolutions() const {
    QVariantList list;
    for (int value : m_options.resolutions)
        list << value;
    return list;
}

QVariantList Backend::pageSizes() const {
    // Names stay in English here; QML translates them, so a language switch
    // re-renders them.
    static const QHash<QString, QString> names{{"a4", "A4"}, {"letter", "Letter"}, {"legal", "Legal"},
                                               {"a5", "A5"}, {"a3", "A3"}, {"full", "Full area"}};
    QVariantList list;
    for (const auto &size : ScanImage::pageSizes())
        list << QVariantMap{{"id", size.id}, {"name", names.value(size.id, size.id)}};
    return list;
}

bool Backend::discovering() const { return m_scanner->isDiscovering(); }
bool Backend::loadingOptions() const { return m_scanner->isQuerying(); }
bool Backend::scanning() const { return m_scanner->isScanning(); }

bool Backend::ready() const {
    // A scanner whose settings could not be read may still scan; trying shows
    // the real error.
    return m_deviceIndex >= 0 && !discovering() && !loadingOptions() && !scanning() && !exporting();
}

void Backend::setDeviceIndex(int index) {
    if (index == m_deviceIndex || index >= m_devices.size())
        return;
    m_deviceIndex = index;
    emit deviceIndexChanged();
    m_options = {};
    emit optionsChanged();
    if (index < 0) {
        emit busyChanged();
        return;
    }
    m_settings.device = m_devices[index].id;
    m_store.setValue("device", m_devices[index].id);
    m_store.setValue("deviceName", m_devices[index].name);
    m_scanner->queryOptions(m_devices[index].id);
    emit busyChanged();
}

void Backend::applyOptions(const ScanImage::Options &options) {
    m_options = options;
    auto pick = [](const QStringList &values, const QString &saved, const QString &fallback) {
        return values.contains(saved) ? saved : values.contains(fallback) ? fallback : values.value(0);
    };
    m_settings.source = pick(options.sources, m_settings.source, options.defaultSource);
    m_settings.mode = pick(options.modes, m_settings.mode, options.defaultMode);
    // 300 dpi is what documents want; a scanner's own default is often a
    // preview resolution.
    const auto &values = options.resolutions;
    m_settings.resolution = values.contains(m_settings.resolution) ? m_settings.resolution
                          : values.contains(300) ? 300
                          : values.contains(options.defaultResolution) ? options.defaultResolution
                                                                       : values.value(0);
    emit optionsChanged();
    emit settingsChanged();
}

void Backend::save() {
    m_store.setValue("source", m_settings.source);
    m_store.setValue("mode", m_settings.mode);
    m_store.setValue("resolution", m_settings.resolution);
    m_store.setValue("page", m_settings.page);
    emit settingsChanged();
}

void Backend::setSource(const QString &source) {
    if (source == m_settings.source || !m_options.sources.contains(source))
        return;
    m_settings.source = source;
    save();
}

void Backend::setMode(const QString &mode) {
    if (mode == m_settings.mode || !m_options.modes.contains(mode))
        return;
    m_settings.mode = mode;
    save();
}

void Backend::setResolution(int resolution) {
    if (resolution == m_settings.resolution || !m_options.resolutions.contains(resolution))
        return;
    m_settings.resolution = resolution;
    save();
}

void Backend::setPageSize(const QString &page) {
    if (page == m_settings.page || ScanImage::pageSize(page).id != page)
        return;
    m_settings.page = page;
    save();
}

void Backend::cycleSource() {
    const auto &list = m_options.sources;
    if (!list.isEmpty())
        setSource(list[(list.indexOf(m_settings.source) + 1) % list.size()]);
}

void Backend::cycleMode() {
    const auto &list = m_options.modes;
    if (!list.isEmpty())
        setMode(list[(list.indexOf(m_settings.mode) + 1) % list.size()]);
}

void Backend::cyclePageSize() {
    const auto sizes = ScanImage::pageSizes();
    int index = 0;
    for (int i = 0; i < sizes.size(); ++i)
        if (sizes[i].id == m_settings.page)
            index = i;
    setPageSize(sizes[(index + 1) % sizes.size()].id);
}

void Backend::stepResolution(int steps) {
    const auto &list = m_options.resolutions;
    if (list.isEmpty())
        return;
    const int index = qBound(0, int(list.indexOf(m_settings.resolution)) + steps, int(list.size()) - 1);
    setResolution(list[index]);
}

void Backend::discover() {
    if (scanning())
        return;
    m_scanner->discover();
    emit busyChanged();
}

void Backend::scan() {
    m_retried = false;
    m_retryScanner.clear();
    startScan();
}

void Backend::startScan() {
    if (!ready())
        return;
    if (!m_session->isValid()) {
        setError(tr("Could not create a folder for the scans: %1").arg(m_session->errorString()));
        return;
    }
    dismissError();
    m_scannedPages = 0;
    m_progress = 0;
    emit progressChanged();
    const QString pattern = m_session->filePath(QString("scan-%1-%04d.png").arg(++m_run));
    m_scanner->scan(m_settings, m_options, pattern);
    emit busyChanged();
}

void Backend::cancelScan() {
    m_scanner->cancel();
}

QString Backend::suggestedExportPath() const {
    QString folder = m_store.value("exportFolder").toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QDir::homePath();
    return folder + "/scan-" + QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm") + ".pdf";
}

void Backend::exportPdf() {
    if (m_pages.count() == 0 || exporting() || scanning())
        return;
    m_picker->savePdf(QUrl::fromLocalFile(suggestedExportPath()));
}

void Backend::exportTo(const QUrl &url) {
    if (m_pages.count() == 0 || exporting())
        return;
    if (!url.isLocalFile()) {
        setError(tr("Can only export to a local file."));
        return;
    }
    dismissError();
    m_exportPath = url.toLocalFile();
    m_exportRevision = m_pages.revision();
    const QList<Page> pages = m_pages.pages();
    const QString path = m_exportPath;
    m_export.setFuture(QtConcurrent::run([pages, path] {
        QString error;
        PdfExport::write(pages, path, &error);
        return error;
    }));
    emit busyChanged();
}

void Backend::newDocument() {
    if (scanning())
        return;
    m_pages.clear();
    dismissError();
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::dismissError() {
    setError({});
}

bool Backend::dirty() const {
    return m_dirty;
}

void Backend::updateDirty() {
    const bool dirty = m_pages.count() > 0 && m_pages.revision() != m_exportedRevision;
    if (dirty != m_dirty) {
        m_dirty = dirty;
        emit dirtyChanged();
    }
}
