#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>

class Ollama;
class QNetworkReply;

// One question to the local model about a passage, for QML: explain it,
// summarize it, quiz on it, or answer a question about it. The answer
// streams into `answer` as it is written.
class AiRequest : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString answer READ answer NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString action READ action NOTIFY changed)

public:
    using QObject::QObject;
    ~AiRequest() override;

    // The Ollama every request talks to, set once at startup.
    static void setOllama(Ollama *ollama);

    bool running() const { return m_running; }
    QString answer() const { return m_answer; }
    QString error() const { return m_error; }
    QString action() const { return m_action; }

    // `action` is "explain", "summarize", "quiz", or "ask". `input` holds
    // text (the passage), and optionally context (the text around it),
    // title (the book), and question (for "ask").
    Q_INVOKABLE void run(const QString &action, const QVariantMap &input);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    // The prompt for an action, kept separate so tests can read it.
    static QString prompt(const QString &action, const QVariantMap &input);
    static QString systemPrompt();
    // How to write formulas, diagrams and charts so Shelf can draw them.
    static QString visualGuide();

signals:
    void changed();
    void finished();

private:
    QPointer<QNetworkReply> m_reply;
    bool m_running = false;
    QString m_answer;
    QString m_error;
    QString m_action;
    int m_generation = 0;
};
