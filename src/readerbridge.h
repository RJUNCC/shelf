#pragma once

#include <QObject>
#include <QString>

// The EPUB reader page talks to QML through this object over Qt WebChannel.
// The page calls the slots; QML listens to the signals.
class ReaderBridge : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void reportReady(const QString &tocJson) { emit ready(tocJson); }
    // `details` is JSON: minutes left in the chapter and the book, and the
    // section starts as fractions, for the footer's slider.
    void reportRelocated(const QString &cfi, double fraction, const QString &chapter, const QString &details) {
        emit relocated(cfi, fraction, chapter, details);
    }
    void reportFailed(const QString &message) { emit failed(message); }
    // A passage was selected: its CFI, text, box in the view's pixels, and
    // the text around it (for AI answers that fit the context).
    void reportSelection(const QString &cfi, const QString &text, double x, double y, double width, double height,
                         const QString &context) {
        emit selected(cfi, text, x, y, width, height, context);
    }
    void reportSelectionCleared() { emit selectionCleared(); }
    // The mouse was released over the page, so QML can take the keyboard
    // back for its shortcuts.
    void reportPointerUp() { emit pointerReleased(); }
    // An existing highlight was clicked.
    void reportAnnotationClicked(const QString &cfi, double x, double y, double width, double height) {
        emit annotationClicked(cfi, x, y, width, height);
    }

signals:
    void ready(const QString &tocJson);
    void relocated(const QString &cfi, double fraction, const QString &chapter, const QString &details);
    void failed(const QString &message);
    void selected(const QString &cfi, const QString &text, double x, double y, double width, double height,
                  const QString &context);
    void selectionCleared();
    void pointerReleased();
    void annotationClicked(const QString &cfi, double x, double y, double width, double height);
};
