#include "scanner.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace {
// Discovery waits on mDNS and the network, which takes a few seconds; a
// scanner that has not answered in this long is not going to.
constexpr int queryTimeout = 45000;

QString failure(QProcess &process, const QString &stderrText) {
    const QString message = ScanImage::errorMessage(stderrText);
    if (!message.isEmpty())
        return message;
    if (process.exitStatus() == QProcess::CrashExit)
        return QObject::tr("scanimage crashed.");
    return QObject::tr("scanimage failed with exit code %1.").arg(process.exitCode());
}
} // namespace

Scanner::Scanner(const QString &program, QObject *parent) : QObject(parent) {
    qRegisterMetaType<ScanImage::Device>();
    qRegisterMetaType<ScanImage::Options>();

    const QString requested = !program.isEmpty() ? program
                            : !qEnvironmentVariableIsEmpty("OMASCAN_SCANIMAGE") ? qEnvironmentVariable("OMASCAN_SCANIMAGE")
                                                                                : QStringLiteral("scanimage");
    m_program = requested.contains('/') ? requested : QStandardPaths::findExecutable(requested);

    for (QTimer *timer : {&m_discoverTimeout, &m_optionsTimeout}) {
        timer->setSingleShot(true);
        timer->setInterval(queryTimeout);
    }
    connect(&m_discoverTimeout, &QTimer::timeout, &m_discover, &QProcess::kill);
    connect(&m_optionsTimeout, &QTimer::timeout, &m_options, &QProcess::kill);

    connect(&m_discover, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_discoverTimeout.stop();
        const QString output = QString::fromUtf8(m_discover.readAllStandardOutput());
        const QString errors = QString::fromUtf8(m_discover.readAllStandardError());
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        auto devices = ScanImage::parseDevices(output);
        // SANE's test backend is never listed, only opened by name; this
        // makes it a scanner to develop against without hardware.
        if (qEnvironmentVariableIntValue("OMASCAN_TEST_SCANNER"))
            devices << ScanImage::Device{"test", "SANE test scanner", {}};
        emit devicesFound(devices, ok ? QString() : failure(m_discover, errors));
    });
    connect(&m_discover, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit devicesFound({}, tr("Could not run scanimage."));
    });

    connect(&m_options, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_optionsTimeout.stop();
        const QString output = QString::fromUtf8(m_options.readAllStandardOutput());
        const QString errors = QString::fromUtf8(m_options.readAllStandardError());
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        emit optionsFound(m_optionsDevice, ScanImage::parseOptions(output),
                          ok ? QString() : failure(m_options, errors));
    });
    connect(&m_options, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit optionsFound(m_optionsDevice, {}, tr("Could not run scanimage."));
    });

    connect(&m_scan, &QProcess::readyReadStandardOutput, this, &Scanner::readScanOutput);
    connect(&m_scan, &QProcess::readyReadStandardError, this, [this] {
        const QString chunk = QString::fromUtf8(m_scan.readAllStandardError());
        // Progress lines repeat many times a second; keep the tail, which is
        // where the error that ends a scan is printed.
        m_scanStderr = (m_scanStderr + chunk).right(16384);
        const double percent = ScanImage::parseProgress(chunk);
        if (percent >= 0)
            emit progress(percent);
    });
    connect(&m_scan, &QProcess::finished, this, &Scanner::finishScan);
    connect(&m_scan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit scanFinished(0, tr("Could not run scanimage."), false);
    });
}

Scanner::~Scanner() {
    for (QProcess *process : {&m_discover, &m_options, &m_scan})
        stop(*process);
}

void Scanner::stop(QProcess &process) {
    if (process.state() == QProcess::NotRunning)
        return;
    process.blockSignals(true);
    process.terminate();
    if (!process.waitForFinished(2000)) {
        process.kill();
        process.waitForFinished(1000);
    }
    process.blockSignals(false);
}

QString Scanner::missingMessage() const {
    if (m_program.isEmpty() || !QFileInfo(m_program).isExecutable())
        return tr("SANE's scanimage is not installed. Install it with: omarchy-pkg-add sane sane-airscan");
    return {};
}

void Scanner::discover() {
    if (isDiscovering())
        return;
    if (const QString missing = missingMessage(); !missing.isEmpty()) {
        emit devicesFound({}, missing);
        return;
    }
    m_discover.start(m_program, {"-f", ScanImage::deviceListFormat()});
    m_discoverTimeout.start();
}

void Scanner::queryOptions(const QString &device) {
    // Only the newest question matters when the device changes quickly.
    stop(m_options);
    m_optionsDevice = device;
    if (const QString missing = missingMessage(); !missing.isEmpty()) {
        emit optionsFound(device, {}, missing);
        return;
    }
    m_options.start(m_program, {"-d", device, "-A"});
    m_optionsTimeout.start();
}

void Scanner::scan(const ScanImage::Settings &settings, const ScanImage::Options &options, const QString &pattern) {
    if (isScanning())
        return;
    if (const QString missing = missingMessage(); !missing.isEmpty()) {
        emit scanFinished(0, missing, false);
        return;
    }
    m_scanStdout.clear();
    m_scanStderr.clear();
    m_pages = 0;
    m_cancelled = false;
    m_feeder = ScanImage::isFeeder(settings.source);
    m_scan.start(m_program, ScanImage::scanArguments(settings, options, pattern));
}

void Scanner::cancel() {
    if (!isScanning())
        return;
    // scanimage cancels the scan with the scanner on SIGTERM, which leaves
    // the scanner ready for the next one; kill only if it won't stop.
    m_cancelled = true;
    m_scan.terminate();
    QTimer::singleShot(5000, &m_scan, [this] {
        if (isScanning())
            m_scan.kill();
    });
}

void Scanner::readScanOutput() {
    m_scanStdout += m_scan.readAllStandardOutput();
    int newline;
    while ((newline = m_scanStdout.indexOf('\n')) >= 0) {
        const QString path = QString::fromUtf8(m_scanStdout.left(newline)).trimmed();
        m_scanStdout.remove(0, newline + 1);
        // --batch-print names each page once its file is complete.
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            ++m_pages;
            emit pageScanned(path);
        }
    }
}

void Scanner::finishScan(int exitCode, QProcess::ExitStatus status) {
    readScanOutput();
    m_scanStderr += QString::fromUtf8(m_scan.readAllStandardError());

    QString error;
    if (!m_cancelled) {
        const QString message = ScanImage::errorMessage(m_scanStderr);
        if (status != QProcess::NormalExit || exitCode != 0)
            error = failure(m_scan, m_scanStderr);
        else if (m_pages == 0 && m_feeder)
            error = tr("The document feeder is empty.");
        else if (m_pages == 0)
            error = message.isEmpty() ? tr("The scanner returned no pages.") : message;
    }
    emit scanFinished(m_pages, error, m_cancelled);
}
