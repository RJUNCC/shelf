#pragma once

#include <QObject>
#include <QJsonObject>
#include <QVariant>
#include <functional>

class Ollama;

// A practice test on part of a book: the local model writes questions from
// the chapter's own text (multiple choice, free response, or both), each
// with its answer and the reason. Multiple choice is marked at once; a
// free answer is marked by the model against the expected answer and the
// chapter, with feedback.
//
// Questions are maps: {type: "choice"|"free"|"code"|"markov", question,
// choices (choice only), answer (the right choice's index, or the model
// answer), why}. Hands-on questions also carry what their tool needs:
// code has language, starter, solution and tests [{call, expected}];
// markov has task ("build"|"steady"|"steps"), states, matrix, start, steps,
// and expected (the matrix, or the computed probabilities); molecule has
// smiles and formula; scenario has variables, outputs, task ("predict" |
// "find"), output, vary, goal, target, and the computed expected and
// tolerance.
class PracticeTest : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)          // "", "writing", "ready", "failed"
    Q_PROPERTY(QVariantList questions READ questions NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString note READ note NOTIFY changed)            // why the test isn't quite what was asked

public:
    explicit PracticeTest(QObject *parent = nullptr);

    static void setOllama(Ollama *ollama);

    QString state() const { return m_state; }
    QVariantList questions() const { return m_questions; }
    QString error() const { return m_error; }
    QString note() const { return m_note; }

    // `types`: "choice", "free", "handson", or "mixed".
    Q_INVOKABLE void generate(const QString &chapterText, const QString &title, int count, const QString &types);
    // Marks a free answer; the verdict arrives in graded(index, correct,
    // credit, error), where `credit` is the part of the answer that was right, if
    // any, checked to be the student's own words.
    Q_INVOKABLE void grade(int index, const QString &answer);
    Q_INVOKABLE void reset();

    // The questions in a model's JSON answer, checked and cleaned; for tests.
    static QVariantList parseQuestions(const QByteArray &json);
    // The long-run share of time in each state of a chain, or empty when it
    // has no single one; and where a chain is after `steps` steps from a
    // state.
    static QVariantList steadyState(const QList<QList<double>> &matrix);
    static QVariantList afterSteps(const QList<QList<double>> &matrix, int start, int steps);
    // Calls a function of the tools' shared scripts (chem.js, expr.js) and
    // returns its result.
    static QJsonObject runScript(const QString &function, const QStringList &arguments);
    // Whether an answer says nothing to mark ("idk", "?").
    static bool isNonAnswer(const QString &answer);
    // The model's credit if the student really wrote it, otherwise empty.
    static QString groundedCredit(const QString &credit, const QString &answer);

signals:
    void changed();
    void graded(int index, bool correct, const QString &credit, const QString &error);

private:
    // Asks for `count` questions of the given kinds; the result goes to
    // `then` if given (with the questions so far), otherwise to finish().
    void write(const QStringList &kinds, int count, int generation, const std::function<void(QVariantList)> &then,
               const QVariantList &before = {});
    void finish(int generation, QVariantList questions, const QString &error);

    QString m_state, m_error, m_text, m_title, m_note;
    QVariantList m_questions;
    int m_generation = 0;
};
