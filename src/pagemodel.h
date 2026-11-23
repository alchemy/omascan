#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSizeF>
#include <QString>

// One scanned page: the PNG scanimage wrote, which is never changed, and the
// turn to apply when the document is exported.
struct Page {
    // Spelled out so Page{path, rotation, dpi} leaves the size to be read
    // from the file, without a missing-initializer warning at every call.
    Page(const QString &path = {}, int rotation = 0, int dpi = 0, QSizeF size = {})
        : path(path), rotation(rotation), dpi(dpi), size(size) {}

    QString path;
    int rotation = 0; // clockwise degrees: 0, 90, 180 or 270
    int dpi = 0;
    // The page's size on paper in points, unturned. Read from the file when
    // the page is added, so the window can lay pages out before they load.
    QSizeF size;
    bool operator==(const Page &other) const {
        return path == other.path && rotation == other.rotation && dpi == other.dpi && size == other.size;
    }
};

// The pages of the document being scanned, in order, with undo and redo.
// Every change is recorded as a snapshot of the list, which is a few strings
// per page, so undo can never disagree with what is on screen.
class PageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // The width of the widest page as it is turned, in points, so the window
    // can fit every page to its width.
    Q_PROPERTY(double widest READ widest NOTIFY changed)

public:
    enum Role { UrlRole = Qt::UserRole + 1, PathRole, RotationRole, WidthRole, HeightRole };

    explicit PageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_pages.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    double widest() const;
    QList<Page> pages() const { return m_pages; }
    // Bumped on every change, so the app can tell whether what it exported
    // is still what is on screen.
    int revision() const { return m_revision; }

    // Records the current pages as one undo step. A scan calls it once and
    // then appends each page as it arrives, so one undo removes the scan.
    void checkpoint();
    void append(const Page &page);

    Q_INVOKABLE void rotate(int row, int degrees);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE QString urlAt(int row) const;
    Q_INVOKABLE int rotationAt(int row) const;

signals:
    void countChanged();
    void historyChanged();
    void changed();

private:
    void restore(const QList<Page> &pages);
    void touched();

    QList<Page> m_pages;
    QList<QList<Page>> m_undo;
    QList<QList<Page>> m_redo;
    int m_revision = 0;
};
