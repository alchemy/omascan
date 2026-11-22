#pragma once

#include <QList>
#include <QString>

#include "pagemodel.h"

namespace PdfExport {

// Writes the pages to one PDF, each page the size of its scan at its
// resolution and turned as asked. The file is written beside `path` and
// renamed into place, so a failed export never leaves half a document.
// Safe to call off the GUI thread.
bool write(const QList<Page> &pages, const QString &path, QString *error);

} // namespace PdfExport
