#include <QDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "filepicker.h"
#include "pagemodel.h"
#include "pdfexport.h"
#include "scanimage.h"
#include "scanner.h"
#include "theme.h"
#include "translator.h"

namespace {

// Captured from `scanimage -f '%d|%v|%m|%t%n'` on a network with two Brother
// MFC-J6955DW printers and a laptop webcam.
const char *realListing =
    "v4l:/dev/video0|Noname|Laptop Webcam Module (2nd Gen):|virtual device\n"
    "escl:http://192.168.2.31:80|Brother|MFC-J6955DW [94ddf8163b05]|platen,adf scanner\n"
    "escl:http://192.168.2.36:80|Brother|MFC-J6955DW|platen,adf scanner\n"
    "airscan:e0:Brother MFC-J6955DW|eSCL|Brother MFC-J6955DW|ip=192.168.2.36\n"
    "airscan:e1:Brother MFC-J6955DW [94ddf8163b05]|eSCL|Brother MFC-J6955DW [94ddf8163b05]|ip=192.168.2.31\n";

// Captured from `scanimage -d airscan:e1:... -A` for the Brother.
const char *brotherHelp = R"(
All options specific to device `airscan:e1:Brother MFC-J6955DW [94ddf8163b05]':
  Standard:
    --resolution 100|200|300|600dpi [300]
        Sets the resolution of the scanned image.
    --mode Color|Gray [Color]
        Selects the scan mode (e.g., lineart, monochrome, or color).
    --scan-intent *unset*|Document|Text and Graphic|Photo|Preview [*unset*]
        Optimize scan for Text/Photo/etc.
    --source Flatbed|ADF|ADF Duplex [Flatbed]
        Selects the scan source (such as a document-feeder).
  Geometry:
    -l 0..296.926mm [0]
        Top-left x position of scan area.
    -t 0..431.8mm [0]
        Top-left y position of scan area.
    -x 0..296.926mm [296.926]
        Width of scan-area.
    -y 0..431.8mm [431.8]
        Height of scan-area.
  Enhancement:
    --brightness -100..100% (in steps of 1) [0]
        Controls the brightness of the acquired image.
    --negative[=(yes|no)] [no]
        Swap black and white
    --adf-justification-x <string> [inactive]
        ADF width justification (left/right/center)
)";

// From SANE's test backend, which takes any resolution in a range.
const char *testHelp = R"(
All options specific to device `test':
  Scan Mode:
    --mode Gray|Color [Gray]
        Selects the scan mode (e.g., lineart, monochrome, or color).
    --depth 1|8|16 [8]
        Number of bits per sample.
    --resolution 1..1200dpi (in steps of 1) [50]
        Sets the resolution of the scanned image.
    --source Flatbed|Automatic Document Feeder [Flatbed]
        If Automatic Document Feeder is selected, the feeder will be 'empty'
  Geometry:
    -l 0..200mm (in steps of 1) [0]
        Top-left x position of scan area.
    -x 0..200mm (in steps of 1) [80]
        Width of scan-area.
    -y 0..200mm (in steps of 1) [100]
        Height of scan-area.
)";

bool write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QString pngFixture(const QString &directory, int width = 170, int height = 220) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    image.setDotsPerMeterX(qRound(100 / 0.0254));
    image.setDotsPerMeterY(qRound(100 / 0.0254));
    const QString path = directory + QString("/fixture-%1x%2.png").arg(width).arg(height);
    image.save(path);
    return path;
}

// A scanimage that answers like the Brother: one scanner, its options, and
// a scan of three pages; or, with an empty feeder, none.
QString fakeScanimage(const QString &directory, bool emptyFeeder = false) {
    const QString fixture = pngFixture(directory);
    QFile::copy(fixture, directory + "/fixture.png");
    write(directory + "/help.txt", brotherHelp);
    const QString script = directory + "/scanimage";
    QByteArray body = "#!/bin/sh\n"
                      "case \"$*\" in\n"
                      "  *'-f '*) printf '%s\\n' 'v4l:/dev/video0|Noname|Cam|virtual device' "
                      "'airscan:e0:Fake Scanner|eSCL|Fake Scanner|ip=10.0.0.2'; exit 0 ;;\n"
                      "  *' -A'*) cat '" + directory.toUtf8() + "/help.txt'; exit 0 ;;\n"
                      "esac\n"
                      "echo \"$@\" > '" + directory.toUtf8() + "/arguments.txt'\n"
                      "for a; do case \"$a\" in --batch=*) pattern=\"${a#--batch=}\" ;; esac; done\n";
    if (emptyFeeder)
        body += "echo 'scanimage: sane_start: Document feeder out of documents' >&2\nexit 0\n";
    else
        body += "for i in 1 2 3; do f=$(printf \"$pattern\" $i); cp '" + directory.toUtf8() +
                "/fixture.png' \"$f\"; printf 'Progress: 50.0%%\\r' >&2; echo \"$f\"; done\n";
    write(script, body);
    QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return script;
}

class FakePicker : public FilePicker {
public:
    QUrl answer;
    void savePdf(const QUrl &) override { emit saveSelected(answer); }
};

int pdfPageCount(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;
    const QString text = QString::fromLatin1(file.readAll());
    return text.count(QRegularExpression("/Type\\s*/Page(?![s\\w])"));
}

} // namespace

class OmascanTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Keeps the backend's settings and cache out of the real home.
        QStandardPaths::setTestModeEnabled(true);
    }

    void devicesDropWebcamsAndDuplicates() {
        const auto devices = ScanImage::parseDevices(realListing);
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices[0].id, QString("airscan:e0:Brother MFC-J6955DW"));
        QCOMPARE(devices[0].name, QString("Brother MFC-J6955DW"));
        QCOMPARE(devices[0].address, QString("192.168.2.36"));
        QCOMPARE(devices[1].address, QString("192.168.2.31"));
    }

    void devicesKeepEsclWithoutAirscan() {
        const auto devices = ScanImage::parseDevices("escl:http://10.0.0.9:80|Canon|TR4700 series|platen scanner\n"
                                                     "pixma:04A91234|CANON|Canon PIXMA TR4700|multi-function peripheral\n");
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices[0].name, QString("Canon TR4700 series"));
        QCOMPARE(devices[0].address, QString("10.0.0.9"));
        QCOMPARE(devices[1].name, QString("Canon PIXMA TR4700"));
    }

    void optionsFromBrother() {
        const auto options = ScanImage::parseOptions(brotherHelp);
        QCOMPARE(options.sources, QStringList({"Flatbed", "ADF", "ADF Duplex"}));
        QCOMPARE(options.modes, QStringList({"Color", "Gray"}));
        QCOMPARE(options.resolutions, QList<int>({100, 200, 300, 600}));
        QCOMPARE(options.defaultResolution, 300);
        QCOMPARE(options.defaultSource, QString("Flatbed"));
        QCOMPARE(options.width.max, 296.926);
        QCOMPARE(options.height.max, 431.8);
    }

    void optionsFromRange() {
        const auto options = ScanImage::parseOptions(testHelp);
        QCOMPARE(options.resolutions, QList<int>({75, 100, 150, 200, 300, 400, 600, 1200}));
        QCOMPARE(options.defaultResolution, 50);
        QCOMPARE(options.sources.last(), QString("Automatic Document Feeder"));
        QCOMPARE(options.width.max, 200.0);
    }

    void feederSources() {
        QVERIFY(ScanImage::isFeeder("ADF"));
        QVERIFY(ScanImage::isFeeder("ADF Duplex"));
        QVERIFY(ScanImage::isFeeder("Automatic Document Feeder"));
        QVERIFY(!ScanImage::isFeeder("Flatbed"));
        QVERIFY(!ScanImage::isFeeder("Transparency Unit"));
    }

    void flatbedScansOnePageClampedToTheGlass() {
        const auto options = ScanImage::parseOptions(testHelp);
        const auto arguments = ScanImage::scanArguments({"test", "Flatbed", "Color", 300, "a4"}, options, "/d/p-%04d.png");
        QCOMPARE(arguments.mid(0, 8), QStringList({"-d", "test", "--source", "Flatbed", "--mode", "Color", "--resolution", "300"}));
        QCOMPARE(arguments.mid(arguments.indexOf("-x"), 4), QStringList({"-x", "200", "-y", "200"}));
        QVERIFY(arguments.contains("--batch=/d/p-%04d.png"));
        QVERIFY(arguments.contains("--batch-count=1"));
    }

    void feederScansUntilEmpty() {
        const auto options = ScanImage::parseOptions(brotherHelp);
        const auto arguments = ScanImage::scanArguments({"brother", "ADF Duplex", "Gray", 200, "letter"}, options, "/d/p-%04d.png");
        QVERIFY(!arguments.contains("--batch-count=1"));
        QCOMPARE(arguments.mid(arguments.indexOf("-x"), 4), QStringList({"-x", "215.9", "-y", "279.4"}));
        QVERIFY(arguments.contains("--batch-print"));
    }

    void fullAreaAndUnknownValuesAreLeftToTheScanner() {
        const auto options = ScanImage::parseOptions(brotherHelp);
        const auto arguments = ScanImage::scanArguments({"brother", "Film", "Lineart", 300, "full"}, options, "/d/p-%04d.png");
        QVERIFY(!arguments.contains("-x"));
        QVERIFY(!arguments.contains("--source"));
        QVERIFY(!arguments.contains("--mode"));
    }

    void progressAndErrors() {
        QCOMPARE(ScanImage::parseProgress("Progress: 6.3%\rProgress: 12.6%\r"), 12.6);
        QCOMPARE(ScanImage::parseProgress("Scanning page 2"), -1.0);
        QCOMPARE(ScanImage::errorMessage("scanimage: rounded value of br-x from 210 to 200\n"
                                         "scanimage: sane_start: Document feeder out of documents\n"),
                 QString("Document feeder out of documents"));
        QCOMPARE(ScanImage::errorMessage("scanimage: rounded value of br-x from 210 to 200\n"), QString());
    }

    void pagesUndoAScanAtOnce() {
        PageModel pages;
        pages.checkpoint();
        pages.append({"/a.png", 0, 300});
        pages.append({"/b.png", 0, 300});
        QCOMPARE(pages.count(), 2);
        pages.undo();
        QCOMPARE(pages.count(), 0);
        pages.redo();
        QCOMPARE(pages.count(), 2);
        QVERIFY(!pages.canRedo());
    }

    void pagesRotateMoveAndRemove() {
        PageModel pages;
        pages.append({"/a.png", 0, 300});
        pages.append({"/b.png", 0, 300});
        pages.append({"/c.png", 0, 300});
        pages.rotate(0, -90);
        QCOMPARE(pages.rotationAt(0), 270);
        pages.rotate(0, 180);
        QCOMPARE(pages.rotationAt(0), 90);
        pages.move(0, 2);
        QCOMPARE(pages.pages()[2].path, QString("/a.png"));
        pages.move(2, 0);
        QCOMPARE(pages.pages()[0].path, QString("/a.png"));
        pages.remove(1);
        QCOMPARE(pages.count(), 2);
        pages.undo();
        QCOMPARE(pages.pages()[1].path, QString("/b.png"));
        pages.undo();
        pages.undo();
        pages.undo();
        QCOMPARE(pages.rotationAt(0), 270);
        const int before = pages.revision();
        pages.rotate(7, 90);
        pages.move(0, 0);
        QCOMPARE(pages.revision(), before);
    }

    void pdfHasOnePagePerScanSizedByResolution() {
        QTemporaryDir dir;
        const QString portrait = pngFixture(dir.path(), 170, 220);
        const QString output = dir.filePath("out.pdf");
        QString error;
        QVERIFY2(PdfExport::write({{portrait, 0, 100}, {portrait, 90, 100}}, output, &error), qPrintable(error));
        QCOMPARE(pdfPageCount(output), 2);
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray pdf = file.readAll();
        QVERIFY(pdf.startsWith("%PDF"));
        // 170 x 220 pixels at 100 dpi is 122.4 x 158.4 points; turned, wide.
        QList<QSizeF> boxes;
        auto it = QRegularExpression(R"(/MediaBox \[0 0 ([\d.]+) ([\d.]+)\])").globalMatch(QString::fromLatin1(pdf));
        while (it.hasNext()) {
            const auto match = it.next();
            boxes << QSizeF(match.captured(1).toDouble(), match.captured(2).toDouble());
        }
        QCOMPARE(boxes.size(), 2);
        QVERIFY2(qAbs(boxes[0].width() - 122.4) < 1 && qAbs(boxes[0].height() - 158.4) < 1, "portrait page size");
        QVERIFY2(qAbs(boxes[1].width() - 158.4) < 1 && qAbs(boxes[1].height() - 122.4) < 1, "turned page size");
        QCOMPARE(QDir(dir.path()).entryList({"out.pdf*"}, QDir::Files | QDir::Hidden).size(), 1);
    }

    void pdfFailsWithoutLeavingAFile() {
        QTemporaryDir dir;
        const QString output = dir.filePath("out.pdf");
        QString error;
        QVERIFY(!PdfExport::write({{dir.filePath("missing.png"), 0, 300}}, output, &error));
        QVERIFY(error.contains("1"));
        QVERIFY(!QFile::exists(output));
        QVERIFY(!PdfExport::write({}, output, &error));
    }

    void backendScansAndExports() {
        QTemporaryDir dir;
        Scanner scanner(fakeScanimage(dir.path()));
        FakePicker picker;
        picker.answer = QUrl::fromLocalFile(dir.filePath("document.pdf"));
        Backend backend(&scanner, &picker);

        QTRY_COMPARE(backend.devices().size(), 1);
        QTRY_VERIFY(backend.ready());
        QCOMPARE(backend.deviceIndex(), 0);
        QCOMPARE(backend.sources(), QStringList({"Flatbed", "ADF", "ADF Duplex"}));
        QCOMPARE(backend.resolution(), 300);

        backend.setSource("ADF");
        backend.scan();
        QTRY_COMPARE(backend.pages()->count(), 3);
        QTRY_VERIFY(!backend.scanning());
        QVERIFY(backend.error().isEmpty());
        QVERIFY(backend.dirty());
        QFile arguments(dir.filePath("arguments.txt"));
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        QVERIFY(arguments.readAll().contains("--source ADF --mode Color --resolution 300"));

        QSignalSpy exported(&backend, &Backend::exported);
        backend.exportPdf();
        QTRY_COMPARE(exported.size(), 1);
        QCOMPARE(pdfPageCount(dir.filePath("document.pdf")), 3);
        QVERIFY(!backend.dirty());

        backend.pages()->rotate(0, 90);
        QVERIFY(backend.dirty());
        backend.pages()->undo();
        backend.pages()->undo();
        QCOMPARE(backend.pages()->count(), 0);
        QVERIFY(!backend.dirty());
    }

    void backendSaysWhenTheFeederIsEmpty() {
        QTemporaryDir dir;
        Scanner scanner(fakeScanimage(dir.path(), true));
        FakePicker picker;
        Backend backend(&scanner, &picker);
        QTRY_VERIFY(backend.ready());
        backend.setSource("ADF");
        backend.scan();
        QTRY_VERIFY(!backend.scanning());
        QTRY_COMPARE(backend.error(), QString("The document feeder is empty."));
        QCOMPARE(backend.pages()->count(), 0);
        QVERIFY(!backend.pages()->canUndo());
    }

    void backendSaysWhenScanimageIsMissing() {
        QTemporaryDir dir;
        Scanner scanner(dir.filePath("no-scanimage"));
        FakePicker picker;
        Backend backend(&scanner, &picker);
        QVERIFY(backend.error().contains("omarchy-pkg-add sane"));
        QVERIFY(!backend.ready());
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QVERIFY(write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n"));
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Scan").isNull());
    }

    void translatorReadsTheBundledItalian() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "it\n"));
        QFile::copy(QFINDTESTDATA("../src/i18n/en.json"), dir.filePath("en.json"));
        QFile::copy(QFINDTESTDATA("../src/i18n/it.json"), dir.filePath("it.json"));
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("it"));
        QCOMPARE(translator.translate("Main", "Scan"), QString("Scansiona"));
        QCOMPARE(translator.translate("Main", "Page %1 of %2"), QString("Pagina %1 di %2"));
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/language", "ru\n"));
        QVERIFY(write(dir.path() + "/en.json", R"({"%n pages": {"one": "%n page", "other": "%n pages"}})"));
        QVERIFY(write(dir.path() + "/ru.json", R"({"%n pages": {"one": "%n страница", "few": "%n страницы", "many": "%n страниц", "other": "%n страницы"}})"));
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n pages", nullptr, 21), QString("%n страница"));
        QCOMPARE(translator.translate("Main", "%n pages", nullptr, 22), QString("%n страницы"));
        QCOMPARE(translator.translate("Main", "%n pages", nullptr, 25), QString("%n страниц"));
    }
};

QTEST_MAIN(OmascanTests)
#include "omascan_tests.moc"
