#include "pagemodel.h"

#include <QImageReader>
#include <QUrl>

namespace {
constexpr int historyLimit = 200;
}

PageModel::PageModel(QObject *parent) : QAbstractListModel(parent) {}

int PageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_pages.size();
}

QVariant PageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_pages.size())
        return {};
    const Page &page = m_pages[index.row()];
    switch (role) {
    case UrlRole: return QUrl::fromLocalFile(page.path);
    case PathRole: return page.path;
    case RotationRole: return page.rotation;
    case WidthRole: return page.size.width();
    case HeightRole: return page.size.height();
    }
    return {};
}

QHash<int, QByteArray> PageModel::roleNames() const {
    return {{UrlRole, "url"}, {PathRole, "path"}, {RotationRole, "turn"}, {WidthRole, "pageWidth"}, {HeightRole, "pageHeight"}};
}

QString PageModel::urlAt(int row) const {
    return row >= 0 && row < m_pages.size() ? QUrl::fromLocalFile(m_pages[row].path).toString() : QString();
}

int PageModel::rotationAt(int row) const {
    return row >= 0 && row < m_pages.size() ? m_pages[row].rotation : 0;
}

void PageModel::checkpoint() {
    m_undo << m_pages;
    if (m_undo.size() > historyLimit)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

void PageModel::touched() {
    ++m_revision;
    emit changed();
}

double PageModel::widest() const {
    double widest = 0;
    for (const Page &page : m_pages)
        widest = qMax(widest, page.rotation % 180 ? page.size.height() : page.size.width());
    return widest > 0 ? widest : 595; // A4, for the empty document
}

void PageModel::append(const Page &scanned) {
    Page page = scanned;
    if (page.size.isEmpty()) {
        QImageReader reader(page.path);
        const QSize pixels = reader.size();
        const int dpi = page.dpi > 0 ? page.dpi : 300;
        page.size = pixels.isValid() ? QSizeF(pixels) * 72.0 / dpi : QSizeF(595, 842);
    }
    beginInsertRows({}, m_pages.size(), m_pages.size());
    m_pages << page;
    endInsertRows();
    emit countChanged();
    touched();
}

void PageModel::rotate(int row, int degrees) {
    if (row < 0 || row >= m_pages.size() || degrees % 90 != 0)
        return;
    checkpoint();
    m_pages[row].rotation = ((m_pages[row].rotation + degrees) % 360 + 360) % 360;
    emit dataChanged(index(row), index(row), {RotationRole});
    touched();
}

void PageModel::remove(int row) {
    if (row < 0 || row >= m_pages.size())
        return;
    checkpoint();
    beginRemoveRows({}, row, row);
    m_pages.removeAt(row);
    endRemoveRows();
    emit countChanged();
    touched();
}

void PageModel::move(int from, int to) {
    if (from < 0 || from >= m_pages.size() || to < 0 || to >= m_pages.size() || from == to)
        return;
    checkpoint();
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_pages.move(from, to);
    endMoveRows();
    touched();
}

void PageModel::clear() {
    if (m_pages.isEmpty())
        return;
    checkpoint();
    restore({});
}

void PageModel::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo << m_pages;
    restore(m_undo.takeLast());
    emit historyChanged();
}

void PageModel::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo << m_pages;
    restore(m_redo.takeLast());
    emit historyChanged();
}

void PageModel::restore(const QList<Page> &pages) {
    const bool sizeChanged = pages.size() != m_pages.size();
    beginResetModel();
    m_pages = pages;
    endResetModel();
    if (sizeChanged)
        emit countChanged();
    touched();
}
