#pragma once

#include <QList>
#include <QObject>
#include <QVariant>

// Flashcards, reviewed with spaced repetition (SM-2, as Anki schedules
// them): a card answered well comes back after longer and longer gaps, one
// forgotten comes back the same session. Cards come from "Quiz me" answers
// or are written by hand, and remember the book and place they came from.
//
// All cards live in one JSON file, saved atomically after every change.
// Each card is a map:
//   id, bookId, front, back, location (where in the book, as for notes)
//   created, due        milliseconds since the epoch
//   interval            days until the next review after a good answer
//   ease                how fast the interval grows (2.5 to start, at least 1.3)
//   reps, lapses        good answers in a row; times forgotten
class Cards : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int dueCount READ dueCount NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)

public:
    enum Grade { Again = 0, Hard = 1, Good = 2, Easy = 3 };
    Q_ENUM(Grade)

    explicit Cards(const QString &file = {}, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    int dueCount() const;
    int totalCount() const { return int(m_cards.size()); }

    // The cards in a "Quiz me" answer: "**Q:** …" lines, each followed by
    // its "**A:** …" (which may run over several lines).
    Q_INVOKABLE static QVariantList parseQuiz(const QString &text);

    Q_INVOKABLE QString add(const QString &bookId, const QString &front, const QString &back,
                            const QString &location = {});
    // Adds the cards in a quiz answer, skipping ones the book already has.
    // Returns how many were added.
    Q_INVOKABLE int addFromQuiz(const QString &bookId, const QString &text, const QString &location = {});
    Q_INVOKABLE void update(const QString &id, const QString &front, const QString &back);
    Q_INVOKABLE void remove(const QString &id);

    // Due now (all books, or one), the longest overdue first.
    Q_INVOKABLE QVariantList due(const QString &bookId = {}, int limit = 200) const;
    Q_INVOKABLE QVariantList list(const QString &bookId = {}) const;
    Q_INVOKABLE int count(const QString &bookId) const;

    Q_INVOKABLE void grade(const QString &id, int grade);
    // When a grade would bring the card back, short: "10 min", "3 d".
    Q_INVOKABLE QString preview(const QString &id, int grade) const;

    // Tests fix the clock.
    void setNow(qint64 ms) { m_now = ms; }

signals:
    void changed();

private:
    struct Schedule {
        double interval;   // days
        double ease;
        int reps;
        int lapses;
        qint64 due;
    };
    Schedule next(const QVariantMap &card, int grade) const;
    qint64 now() const;
    int indexOf(const QString &id) const;
    void load();
    void save();

    QString m_file;
    QList<QVariantMap> m_cards;
    int m_revision = 0;
    qint64 m_now = 0;
};
