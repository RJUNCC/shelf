#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariant>

class Ollama;

// Short animated lessons from the local model: a slide (or a passage, or a
// practice question) explained in a few scenes, each a sentence or three
// of narration and a visual that builds as it's spoken: points, steps, a
// diagram, a formula and what its symbols mean, a graph, bars, two things
// compared, or a quotation from the book.
//
// Every formula is checked by typesetting it, every graph by working it
// out; a scene that fails becomes plain points rather than an error.
// Lessons are kept, so each is written once.
class Explainer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The key of the lesson being written, so a page knows it's its own.
    Q_PROPERTY(QString pending READ pending NOTIFY changed)

public:
    explicit Explainer(Ollama *ollama, const QString &cacheDirectory = {}, QObject *parent = nullptr);

    bool busy() const { return !m_pending.isEmpty(); }
    QString error() const { return m_error; }
    QString pending() const { return m_pending; }

    // `input`: {key, title (what it's about), text (the slide or passage),
    // notes, passages: [{label, text}] from the books, sources (what the
    // passages come from, which identifies the lesson with the rest),
    // question (to answer a follow-up instead)}. The lesson arrives in ready(key, lesson).
    Q_INVOKABLE void explain(const QVariantMap &input);
    // The lesson already written for this input, or an empty map.
    Q_INVOKABLE QVariantMap cached(const QVariantMap &input) const;
    Q_INVOKABLE void cancel();

    // The topics of an exam review (what a teacher says the next exam
    // covers), each with words to find it by: topicsReady(key, [{topic,
    // keywords: [..]}]) or topicsFailed(key, error).
    Q_INVOKABLE void studyTopics(const QString &key, const QString &review);
    static QVariantList parseTopics(const QByteArray &json);

    // The pieces, for tests.
    static QJsonObject lessonSchema();
    static QString prompt(const QVariantMap &input);
    // Checks and tidies a model's lesson; {} when nothing usable is left.
    static QVariantMap parseLesson(const QByteArray &json);
    // Narration cut into sentences, which pace the animation and the voice.
    static QStringList sentences(const QString &text);

signals:
    void changed();
    void ready(const QString &key, const QVariantMap &lesson);
    void topicsReady(const QString &key, const QVariantList &topics);
    void topicsFailed(const QString &key, const QString &error);

private:
    QString cacheFile(const QVariantMap &input) const;

    Ollama *m_ollama;
    QString m_cache;
    QString m_pending, m_error;
    int m_generation = 0;
};
