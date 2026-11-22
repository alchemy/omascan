#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

#include "scanimage.h"

// Runs scanimage. SANE backends crash and hang more than most libraries, so
// each job is its own process: a bad driver kills that job, never the app.
class Scanner : public QObject {
    Q_OBJECT

public:
    // `program` overrides the scanimage to run (tests pass a fake one); the
    // OMASCAN_SCANIMAGE environment variable does the same from outside.
    explicit Scanner(const QString &program = {}, QObject *parent = nullptr);
    ~Scanner() override;

    // Empty when scanimage can be run, otherwise what is missing.
    QString missingMessage() const;

    void discover();
    void queryOptions(const QString &device);
    void scan(const ScanImage::Settings &settings, const ScanImage::Options &options, const QString &pattern);
    void cancel();

    bool isDiscovering() const { return m_discover.state() != QProcess::NotRunning; }
    bool isQuerying() const { return m_options.state() != QProcess::NotRunning; }
    bool isScanning() const { return m_scan.state() != QProcess::NotRunning; }

signals:
    void devicesFound(const QList<ScanImage::Device> &devices, const QString &error);
    void optionsFound(const QString &device, const ScanImage::Options &options, const QString &error);
    void pageScanned(const QString &path);
    void progress(double percent);
    // `error` is empty when the scan worked or was cancelled.
    void scanFinished(int pages, const QString &error, bool cancelled);

private:
    void readScanOutput();
    void finishScan(int exitCode, QProcess::ExitStatus status);
    static void stop(QProcess &process);

    QString m_program;
    QProcess m_discover;
    QProcess m_options;
    QProcess m_scan;
    QTimer m_discoverTimeout;
    QTimer m_optionsTimeout;
    QString m_optionsDevice;
    QByteArray m_scanStdout;
    QString m_scanStderr;
    int m_pages = 0;
    bool m_cancelled = false;
    bool m_feeder = false;
};
