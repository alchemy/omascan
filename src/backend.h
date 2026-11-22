#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <memory>

#include "pagemodel.h"
#include "scanimage.h"

class FilePicker;
class Scanner;

// The scanning session behind the window: which scanner and settings, the
// pages scanned so far, and exporting them as a PDF.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)
    Q_PROPERTY(QStringList sources READ sources NOTIFY optionsChanged)
    Q_PROPERTY(QStringList modes READ modes NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList resolutions READ resolutions NOTIFY optionsChanged)
    Q_PROPERTY(QVariantList pageSizes READ pageSizes CONSTANT)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY settingsChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY settingsChanged)
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY settingsChanged)
    Q_PROPERTY(QString pageSize READ pageSize WRITE setPageSize NOTIFY settingsChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY busyChanged)
    Q_PROPERTY(bool loadingOptions READ loadingOptions NOTIFY busyChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY busyChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY busyChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int scannedPages READ scannedPages NOTIFY progressChanged)
    Q_PROPERTY(bool feeder READ feeder NOTIFY settingsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(PageModel *pages READ pages CONSTANT)

public:
    // Both default to the real ones; tests pass a Scanner running a fake
    // scanimage and a picker that answers on its own.
    explicit Backend(Scanner *scanner = nullptr, FilePicker *picker = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    QVariantList devices() const;
    int deviceIndex() const { return m_deviceIndex; }
    void setDeviceIndex(int index);
    QStringList sources() const { return m_options.sources; }
    QStringList modes() const { return m_options.modes; }
    QVariantList resolutions() const;
    QVariantList pageSizes() const;
    QString source() const { return m_settings.source; }
    void setSource(const QString &source);
    QString mode() const { return m_settings.mode; }
    void setMode(const QString &mode);
    int resolution() const { return m_settings.resolution; }
    void setResolution(int resolution);
    QString pageSize() const { return m_settings.page; }
    void setPageSize(const QString &page);

    bool discovering() const;
    bool loadingOptions() const;
    bool ready() const;
    bool scanning() const;
    bool exporting() const { return m_export.isRunning(); }
    double progress() const { return m_progress; }
    int scannedPages() const { return m_scannedPages; }
    bool feeder() const { return ScanImage::isFeeder(m_settings.source); }
    QString error() const { return m_error; }
    bool dirty() const;
    PageModel *pages() { return &m_pages; }

    Q_INVOKABLE void discover();
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancelScan();
    Q_INVOKABLE void exportPdf();
    Q_INVOKABLE void exportTo(const QUrl &url);
    Q_INVOKABLE void newDocument();
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void cycleSource();
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void cyclePageSize();
    Q_INVOKABLE void stepResolution(int steps);

signals:
    void devicesChanged();
    void deviceIndexChanged();
    void optionsChanged();
    void settingsChanged();
    void busyChanged();
    void progressChanged();
    void errorChanged();
    void dirtyChanged();
    void pageAdded(int row);
    void exported(const QString &path);

private:
    void setError(const QString &error);
    void applyOptions(const ScanImage::Options &options);
    void save();
    void updateDirty();
    QString suggestedExportPath() const;
    static void removeStaleSessions(const QString &cache);

    Scanner *m_scanner;
    FilePicker *m_picker;
    QSettings m_store;
    std::unique_ptr<QTemporaryDir> m_session;
    QList<ScanImage::Device> m_devices;
    int m_deviceIndex = -1;
    ScanImage::Options m_options;
    ScanImage::Settings m_settings;
    PageModel m_pages;
    QFutureWatcher<QString> m_export;
    QString m_exportPath;
    int m_exportRevision = 0;
    int m_exportedRevision = -1;
    bool m_dirty = false;
    double m_progress = 0;
    int m_scannedPages = 0;
    int m_run = 0;
    QString m_error;
};
