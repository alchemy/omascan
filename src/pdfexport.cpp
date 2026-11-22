#include "pdfexport.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTransform>

namespace PdfExport {

namespace {
QString tr(const char *text) {
    return QCoreApplication::translate("PdfExport", text);
}

int resolutionOf(const Page &page, const QImage &image) {
    if (page.dpi > 0)
        return page.dpi;
    const int fromFile = qRound(image.dotsPerMeterX() * 0.0254);
    return fromFile >= 50 ? fromFile : 300;
}

// QPageSize keeps custom sizes upright, so a wide page is an upright size
// laid out in landscape.
QPageLayout layoutFor(const QSizeF &millimetres) {
    const bool wide = millimetres.width() > millimetres.height();
    const QSizeF upright = wide ? millimetres.transposed() : millimetres;
    return QPageLayout(QPageSize(upright, QPageSize::Millimeter, {}, QPageSize::ExactMatch),
                       wide ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF());
}
} // namespace

bool write(const QList<Page> &pages, const QString &path, QString *error) {
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (pages.isEmpty())
        return fail(tr("There are no pages to export."));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(tr("Could not write %1: %2").arg(path, file.errorString()));

    QPdfWriter writer(&file);
    writer.setCreator("omascan");
    writer.setTitle(QFileInfo(path).completeBaseName());

    QPainter painter;
    for (int i = 0; i < pages.size(); ++i) {
        const Page &page = pages[i];
        QImage image(page.path);
        if (image.isNull()) {
            file.cancelWriting();
            return fail(tr("Could not read scanned page %1.").arg(i + 1));
        }
        const int dpi = resolutionOf(page, image);
        if (page.rotation != 0)
            image = image.transformed(QTransform().rotate(page.rotation));

        const QSizeF millimetres(image.width() * 25.4 / dpi, image.height() * 25.4 / dpi);
        writer.setPageLayout(layoutFor(millimetres));
        if (i == 0) {
            if (!painter.begin(&writer)) {
                file.cancelWriting();
                return fail(tr("Could not start the PDF."));
            }
        } else if (!writer.newPage()) {
            painter.end();
            file.cancelWriting();
            return fail(tr("Could not add page %1 to the PDF.").arg(i + 1));
        }
        painter.drawImage(QRectF(0, 0, writer.width(), writer.height()), image);
    }
    painter.end();

    if (!file.commit())
        return fail(tr("Could not write %1: %2").arg(path, file.errorString()));
    return true;
}

} // namespace PdfExport
