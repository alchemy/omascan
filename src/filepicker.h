#pragma once

#include <QObject>
#include <QUrl>

// Where the user wants the document saved. Behind an interface so tests can
// answer without a desktop portal. The pattern follows Omacut (MIT, David
// Heinemeier Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    ~FilePicker() override = default;

    virtual void savePdf(const QUrl &suggestedUrl) = 0;

signals:
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};
