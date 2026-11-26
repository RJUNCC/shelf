#pragma once

#include <QObject>
#include <QStringList>

// Asks the user for files. The real one goes through xdg-desktop-portal;
// tests inject a fake that answers immediately.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual void pickBooks(const QString &title) = 0;

    // What this picker asks for: "books" (EPUB, PDF), "slides" (PDF,
    // PowerPoint) or "documents" (a study review). Workspaces have a picker
    // of their own.
    QString kind = "books";

signals:
    void picked(const QStringList &paths);
    void failed(const QString &reason);
};
