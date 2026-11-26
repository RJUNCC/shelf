#include "practicetest.h"

#include "ollama.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace {
Ollama *shared = nullptr;

const QString systemPrompt = QStringLiteral(
    "You write practice tests for students from the text of their textbook. Every question must be "
    "answerable from the text given; never test something it does not say. Answer only with JSON in the "
    "requested shape. In questions, answers and explanations, write formulas and symbols in LaTeX between "
    "dollar signs, like $F = ma$ or $v_0$ (Shelf typesets them); in multiple-choice options, write them as "
    "plain text with Unicode symbols, like x², √2, ×, →, because options are shown as plain text.");

QJsonObject questionsSchema(const QStringList &allowed) {
    // One shape per kind of question, each with every field it needs
    // required: models leave out fields that are merely allowed.
    const QJsonObject string{{"type", "string"}};
    const QJsonObject strings{{"type", "array"}, {"items", string}};
    const auto kind = [&](const QString &type, QJsonObject properties, QJsonArray required) {
        properties["type"] = QJsonObject{{"type", "string"}, {"enum", QJsonArray{type}}};
        properties["question"] = string;
        properties["answer"] = string;
        properties["why"] = string;
        for (const char *name : {"type", "question", "answer", "why"})
            required << name;
        return QJsonObject{{"type", "object"}, {"properties", properties}, {"required", required}};
    };
    QJsonArray kinds;
    if (allowed.contains("choice"))
        kinds << kind("choice", {{"choices", strings}}, {"choices"});
    if (allowed.contains("free"))
        kinds << kind("free", {}, {});
    {
        const QJsonObject tests{
            {"type", "array"},
            {"items", QJsonObject{{"type", "object"},
                                  {"properties", QJsonObject{{"call", string}, {"expected", string}}},
                                  {"required", QJsonArray{"call", "expected"}}}}};
        if (allowed.contains("code"))
        kinds << kind("code",
                      {{"language", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"python", "javascript"}}}},
                       {"starter", string}, {"solution", string}, {"tests", tests}},
                      {"language", "starter", "solution", "tests"});
        const QJsonObject matrix{
            {"type", "array"}, {"items", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}}}};
        if (allowed.contains("markov"))
        kinds << kind("markov",
                      {{"task", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"build", "steady", "steps"}}}},
                       {"states", strings}, {"matrix", matrix}, {"start", string}, {"steps", QJsonObject{{"type", "integer"}}}},
                      {"task", "states", "matrix", "start", "steps"});
    }
    if (allowed.contains("molecule"))
        kinds << kind("molecule", {{"smiles", string}, {"formula", string}}, {"smiles", "formula"});
    if (allowed.contains("scenario")) {
        const QJsonObject number{{"type", "number"}};
        const QJsonObject variable{
            {"type", "object"},
            {"properties", QJsonObject{{"name", string}, {"label", string}, {"unit", string}, {"min", number},
                                       {"max", number}, {"value", number}}},
            {"required", QJsonArray{"name", "label", "unit", "min", "max", "value"}}};
        const QJsonObject output{
            {"type", "object"},
            {"properties", QJsonObject{{"name", string}, {"label", string}, {"unit", string}, {"formula", string}}},
            {"required", QJsonArray{"name", "label", "unit", "formula"}}};
        kinds << kind("scenario",
                      {{"variables", QJsonObject{{"type", "array"}, {"items", variable}}},
                       {"outputs", QJsonObject{{"type", "array"}, {"items", output}}},
                       {"task", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"predict", "find"}}}},
                       {"output", string}, {"vary", string},
                       {"goal", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"equal", "max", "min"}}}},
                       {"target", number}, {"result", number}},
                      {"variables", "outputs", "task", "output", "vary", "goal", "target", "result"});
    }
    const QJsonObject question = kinds.size() == 1 ? kinds[0].toObject() : QJsonObject{{"anyOf", kinds}};
    return {{"type", "object"},
            {"properties", QJsonObject{{"questions", QJsonObject{{"type", "array"}, {"items", question}}}}},
            {"required", QJsonArray{"questions"}}};
}

QJsonObject verdictSchema() {
    return {{"type", "object"},
            {"properties", QJsonObject{{"right", QJsonObject{{"type", "string"}}},
                                       {"correct", QJsonObject{{"type", "boolean"}}}}},
            {"required", QJsonArray{"right", "correct"}}};
}

// The words that carry meaning: lower case, three letters or more, without
// the little words every sentence has.
QStringList contentWords(const QString &text) {
    static const QSet<QString> little{
        "the", "and", "are", "was", "were", "for", "but", "not", "you", "your", "with", "that", "this", "they",
        "them", "their", "there", "then", "than", "from", "into", "onto", "its", "has", "have", "had", "can",
        "could", "would", "should", "will", "been", "being", "what", "when", "where", "which", "who", "why", "how",
        "all", "any", "some", "more", "most", "very", "also", "just", "only", "because", "about", "does", "did",
        "idk", "dunno", "don", "dont", "know", "nothing", "unsure", "sure", "idea", "clue", "guess", "maybe", "think"};
    QStringList out;
    for (const QString &w : text.toLower().split(QRegularExpression("[^\\p{L}\\p{N}]+"), Qt::SkipEmptyParts))
        if (w.size() >= 3 && !little.contains(w))
            out << w;
    return out;
}

// Whether `word` is among `words`, allowing a different ending
// ("oxidizing" and "oxidized").
bool hasWord(const QStringList &words, const QString &word) {
    const QString stem = word.left(std::max<qsizetype>(4, word.size() - 3));
    for (const QString &w : words)
        if (w == word || (stem.size() >= 4 && w.startsWith(stem)))
            return true;
    return false;
}
} // namespace

QVariantList PracticeTest::steadyState(const QList<QList<double>> &p) {
    // Power iteration on the lazy chain (P + I) / 2, which has the same
    // steady state but never cycles. Started from every state; if they end
    // in different places the chain has no single steady state.
    const qsizetype n = p.size();
    QList<double> first;
    for (qsizetype s = 0; s < n; ++s) {
        QList<double> v(n, 0.0);
        v[s] = 1.0;
        for (int it = 0; it < 20000; ++it) {
            QList<double> next(n, 0.0);
            for (qsizetype i = 0; i < n; ++i)
                for (qsizetype j = 0; j < n; ++j)
                    next[j] += v[i] * (p[i][j] + (i == j ? 1.0 : 0.0)) / 2.0;
            double delta = 0;
            for (qsizetype j = 0; j < n; ++j)
                delta += std::abs(next[j] - v[j]);
            v = next;
            if (delta < 1e-13)
                break;
        }
        if (s == 0) {
            first = v;
            continue;
        }
        for (qsizetype j = 0; j < n; ++j)
            if (std::abs(v[j] - first[j]) > 1e-6)
                return {};
    }
    QVariantList out;
    for (double x : first)
        out << x;
    return out;
}

QVariantList PracticeTest::afterSteps(const QList<QList<double>> &p, int start, int steps) {
    const qsizetype n = p.size();
    QList<double> v(n, 0.0);
    v[start] = 1.0;
    for (int k = 0; k < steps; ++k) {
        QList<double> next(n, 0.0);
        for (qsizetype i = 0; i < n; ++i)
            for (qsizetype j = 0; j < n; ++j)
                next[j] += v[i] * p[i][j];
        v = next;
    }
    QVariantList out;
    for (double x : v)
        out << x;
    return out;
}

QJsonObject PracticeTest::runScript(const QString &function, const QStringList &arguments) {
    // The same scripts the tools run in the web view (src/web/tools), so a
    // question is checked here exactly as it will be marked there.
    static QJSEngine *engine = nullptr;
    if (!engine) {
        engine = new QJSEngine(QCoreApplication::instance());
        for (const char *name : {":/web/tools/chem.js", ":/web/tools/expr.js"}) {
            QFile file(QString::fromLatin1(name));
            file.open(QIODevice::ReadOnly);
            const QJSValue result = engine->evaluate(QString::fromUtf8(file.readAll()), file.fileName());
            if (result.isError())
                qWarning() << "practice test script:" << result.toString();
        }
    }
    QJSValue fn = engine->evaluate(function);
    QJSValueList args;
    for (const QString &a : arguments)
        args << QJSValue(a);
    const QJSValue result = fn.call(args);
    if (result.isError()) {
        qWarning() << "practice test script:" << result.toString();
        return {};
    }
    const QJSValue json = engine->evaluate("JSON.stringify").call({result});
    return QJsonDocument::fromJson(json.toString().toUtf8()).object();
}

bool PracticeTest::isNonAnswer(const QString &answer) {
    // "idk", "I don't know", "?", "no idea": nothing to mark.
    return contentWords(answer).isEmpty();
}

QString PracticeTest::groundedCredit(const QString &credit, const QString &answer) {
    // Small models praise things the student never wrote. Keep the credit
    // only when most of its words really are in the student's answer.
    const QStringList said = contentWords(answer), claimed = contentWords(credit);
    if (claimed.isEmpty() || said.isEmpty())
        return {};
    int found = 0;
    for (const QString &w : claimed)
        found += hasWord(said, w) ? 1 : 0;
    return found * 10 >= claimed.size() * 7 ? credit.trimmed() : QString();
}

PracticeTest::PracticeTest(QObject *parent) : QObject(parent) {}

void PracticeTest::setOllama(Ollama *ollama) { shared = ollama; }

QVariantList PracticeTest::parseQuestions(const QByteArray &json) {
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value("questions").toArray()) {
        const QJsonObject q = v.toObject();
        const QString question = Ollama::repairLatex(q.value("question").toString()).trimmed();
        const QString answer = Ollama::repairLatex(q.value("answer").toString()).trimmed();
        const bool handsOn = QStringList{"code", "markov", "molecule", "scenario"}.contains(q.value("type").toString());
        // Hands-on answers are worked out by Shelf, so only the others need one.
        if (question.isEmpty() || (answer.isEmpty() && !handsOn))
            continue;
        QStringList choices;
        for (const QJsonValue &c : q.value("choices").toArray())
            if (!c.toString().trimmed().isEmpty())
                choices << c.toString().trimmed();
        const QString type = q.value("type").toString();
        const QString why = Ollama::repairLatex(q.value("why").toString()).trimmed();
        if (type == "code") {
            // A function to write, checked by running test calls. A question
            // without a solution or tests can't be checked, so it is dropped.
            const QString solution = q.value("solution").toString().trimmed();
            QVariantList tests;
            for (const QJsonValue &t : q.value("tests").toArray()) {
                const QString call = t.toObject().value("call").toString().trimmed();
                const QString expected = t.toObject().value("expected").toString().trimmed();
                if (!call.isEmpty() && !expected.isEmpty())
                    tests << QVariantMap{{"call", call}, {"expected", expected}};
            }
            if (solution.isEmpty() || tests.isEmpty())
                continue;
            const QString language = q.value("language").toString() == "javascript" ? "javascript" : "python";
            out << QVariantMap{{"type", "code"}, {"question", question}, {"language", language},
                               {"starter", q.value("starter").toString()}, {"solution", solution},
                               {"tests", tests.mid(0, 8)}, {"answer", solution}, {"why", why}};
            continue;
        }
        if (type == "molecule") {
            // The structure must parse, and agree with the formula the model
            // gave: a cheap check that the SMILES is the molecule it named.
            const QString smiles = q.value("smiles").toString().trimmed();
            const QJsonObject checked = runScript("Chem.check", {smiles, q.value("formula").toString()});
            if (smiles.isEmpty() || !checked.value("ok").toBool())
                continue;
            const QString formula = checked.value("formula").toString();
            out << QVariantMap{{"type", "molecule"}, {"question", question}, {"smiles", smiles}, {"formula", formula},
                               {"answer", QStringLiteral("%1 (%2)").arg(answer, formula)}, {"why", why}};
            continue;
        }
        if (type == "scenario") {
            // The formula is worked out here; if it disagrees with the
            // model's own answer, one of them misread the text, so the
            // question is dropped.
            QJsonObject scenario = q;
            const QString task = q.value("task").toString() == "find" ? "find" : "predict";
            scenario["task"] = task;
            const QJsonObject solved = runScript("Expr.solve", {QString::fromUtf8(QJsonDocument(scenario).toJson())});
            if (!solved.value("ok").toBool())
                continue;
            const double expected = solved.value("answer").toDouble();
            const double claimed = q.value("result").toDouble(qQNaN());
            double scale = std::max(std::abs(expected), 1e-9);
            QJsonObject varied;
            for (const QJsonValue &v : q.value("variables").toArray())
                if (v.toObject().value("name").toString() == q.value("vary").toString())
                    varied = v.toObject();
            if (task == "find")
                scale = varied.value("max").toDouble() - varied.value("min").toDouble();
            if (!std::isnan(claimed) && std::abs(claimed - expected) > 0.05 * scale)
                continue;
            QVariantMap item = q.toVariantMap();
            item["type"] = "scenario";
            item["task"] = task;
            item["expected"] = expected;
            item["tolerance"] = 0.02 * scale;
            item["why"] = why;
            // The answer in words, from Shelf's number.
            QJsonObject named;
            const QString wanted = task == "find" ? q.value("vary").toString() : q.value("output").toString();
            for (const QString &list : QStringList{"variables", "outputs"})
                for (const QJsonValue &v : q.value(list).toArray())
                    if (v.toObject().value("name").toString() == wanted)
                        named = v.toObject();
            item["answer"] = QStringLiteral("%1 = %2 %3")
                                 .arg(named.value("label").toString(wanted), QString::number(expected, 'g', 4),
                                      named.value("unit").toString())
                                 .trimmed();
            out << item;
            continue;
        }
        if (type == "markov") {
            // A chain the student builds or works out. The maths is done
            // here, not trusted to the model: rows must be probabilities,
            // and the steady state or n-step answer is computed.
            QStringList states;
            for (const QJsonValue &v : q.value("states").toArray())
                if (!v.toString().trimmed().isEmpty())
                    states << v.toString().trimmed();
            const qsizetype n = states.size();
            QList<QList<double>> matrix;
            for (const QJsonValue &row : q.value("matrix").toArray()) {
                QList<double> r;
                for (const QJsonValue &x : row.toArray())
                    r << x.toDouble(-1);
                matrix << r;
            }
            bool ok = n >= 2 && n <= 6 && matrix.size() == n;
            for (QList<double> &r : matrix) {
                double sum = 0;
                ok = ok && r.size() == n;
                for (double x : r) {
                    ok = ok && x >= 0 && x <= 1;
                    sum += x;
                }
                ok = ok && std::abs(sum - 1.0) <= 0.02;
                if (ok)
                    for (double &x : r)
                        x /= sum;
            }
            if (!ok)
                continue;
            QString task = q.value("task").toString();
            if (task != "steady" && task != "steps")
                task = "build";
            QVariantList rows;
            for (const QList<double> &r : matrix) {
                QVariantList vr;
                for (double x : r)
                    vr << x;
                rows << QVariant(vr);
            }
            int start = std::max<qsizetype>(0, states.indexOf(q.value("start").toString().trimmed()));
            const int steps = std::clamp(q.value("steps").toInt(2), 1, 10);
            QVariant expected = rows;
            if (task == "steady") {
                const QVariantList pi = steadyState(matrix);
                if (pi.isEmpty())
                    continue;
                expected = pi;
            } else if (task == "steps") {
                expected = afterSteps(matrix, start, steps);
            }
            // The answer in words, from the numbers (the model's own words
            // sometimes name the wrong states or sums).
            const auto share = [](double x) { return QString::number(std::round(x * 1000) / 1000); };
            QStringList words;
            if (task == "build") {
                for (qsizetype i = 0; i < n; ++i)
                    for (qsizetype j = 0; j < n; ++j)
                        if (matrix[i][j] > 0)
                            words << QStringLiteral("%1 → %2: %3").arg(states[i], states[j], share(matrix[i][j]));
            } else {
                const QVariantList v = expected.toList();
                for (qsizetype i = 0; i < n; ++i)
                    words << QStringLiteral("%1: %2").arg(states[i], share(v[i].toDouble()));
            }
            out << QVariantMap{{"type", "markov"}, {"task", task}, {"question", question}, {"states", states},
                               {"matrix", rows}, {"start", start}, {"steps", steps}, {"expected", expected},
                               {"answer", words.join(", ")}, {"why", why}};
            continue;
        }
        if (type == "choice" && choices.size() >= 2) {
            // The answer may come as the choice's text, its letter ("B"),
            // or "B) text"; it must be one of the choices.
            int right = -1;
            for (int i = 0; i < choices.size(); ++i) {
                const QString letter = QString(QChar('A' + i));
                if (answer.compare(choices[i], Qt::CaseInsensitive) == 0 || answer.compare(letter, Qt::CaseInsensitive) == 0
                    || answer.startsWith(letter + ")") || answer.startsWith(letter + ".")
                    || choices[i].contains(answer, Qt::CaseInsensitive)) {
                    right = i;
                    break;
                }
            }
            if (right < 0)
                continue;   // a question whose answer is not among its choices is no use
            out << QVariantMap{{"type", "choice"}, {"question", question}, {"choices", choices}, {"answer", right},
                               {"why", why}};
        } else {
            out << QVariantMap{{"type", "free"}, {"question", question}, {"answer", answer}, {"why", why}};
        }
    }
    return out;
}

void PracticeTest::generate(const QString &chapterText, const QString &title, int count, const QString &types) {
    reset();
    if (!shared) {
        m_state = "failed";
        m_error = tr("Local AI is not set up.");
        emit changed();
        return;
    }
    // The chapter, cut to what fits the model with room to write.
    m_text = chapterText.simplified().left(12000);
    if (m_text.size() < 200) {
        m_state = "failed";
        m_error = tr("This part of the book has too little text to write questions from.");
        emit changed();
        return;
    }
    m_title = title;
    m_state = "writing";
    emit changed();
    count = std::clamp(count, 1, 20);
    const int generation = ++m_generation;

    // For trying the tools without a model: questions from a JSON file.
    if (const QString file = qEnvironmentVariable("SHELF_PRACTICE_FILE"); !file.isEmpty()) {
        QFile f(file);
        f.open(QIODevice::ReadOnly);
        finish(generation, parseQuestions(f.readAll()), {});
        return;
    }

    if (types == "choice" || types == "free") {
        write({types}, count, generation, {});
        return;
    }
    if (types != "handson" && types != "mixed") {
        write({"choice", "free"}, count, generation, {});
        return;
    }
    // Hands-on questions only make sense for some chapters. Ask first which
    // tool fits, then write with only that kind allowed: given a choice,
    // models fall back on the easy kinds.
    const QString prompt = QStringLiteral(
        "Book: %1\n\nText:\n\"\"\"\n%2\n\"\"\"\n\nWhich hands-on exercise fits this text? \"code\" if it teaches "
        "programming, algorithms, or something that can be computed with a short function; \"markov\" if it "
        "teaches probability, random processes, or things that move between a few states with given chances; "
        "\"molecule\" if it teaches chemistry or biochemistry with molecules whose structures a student could "
        "draw (functional groups, amino acids, organic compounds); \"scenario\" if it teaches formulas that relate "
        "quantities, such as physics, engineering, finance, or economics, where a student could change the inputs and "
        "see the result; \"none\" otherwise.")
        .arg(title, m_text.left(6000));
    const QJsonObject schema{
        {"type", "object"},
        {"properties", QJsonObject{{"tool", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"code", "markov", "molecule", "scenario", "none"}}}}}},
        {"required", QJsonArray{"tool"}}};
    QPointer<PracticeTest> self(this);
    shared->ask(systemPrompt, prompt, schema, [self, generation, count, types](const QString &answer, const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (!error.isEmpty())
            return self->finish(generation, {}, error);
        const QString tool = QJsonDocument::fromJson(answer.toUtf8()).object().value("tool").toString();
        const bool fits = tool == "code" || tool == "markov" || tool == "molecule" || tool == "scenario";
        if (types == "handson") {
            if (!fits)
                self->m_note = tr("Shelf found no hands-on exercise that fits this part of the book, so these are "
                                  "free-response questions.");
            self->write({fits ? tool : QStringLiteral("free")}, count, generation, {});
        } else if (fits && count >= 3) {
            // Mixed: one hands-on question after the others.
            self->write({"choice", "free"}, count - 1, generation, [self, generation, tool](QVariantList written) {
                self->write({tool}, 1, generation, {}, written);
            });
        } else {
            self->write({"choice", "free"}, count, generation, {});
        }
    });
}

void PracticeTest::write(const QStringList &kinds, int count, int generation,
                         const std::function<void(QVariantList)> &then, const QVariantList &before) {
    QStringList asks;
    if (kinds.contains("choice") && kinds.contains("free"))
        asks << QStringLiteral("questions, about half multiple choice (type \"choice\", four choices each) and half "
                               "free response (type \"free\")");
    else if (kinds == QStringList{"choice"})
        asks << QStringLiteral("multiple-choice questions (type \"choice\") with four choices each");
    else if (kinds == QStringList{"free"})
        asks << QStringLiteral("free-response questions (type \"free\") that need a sentence or two to answer");
    else if (kinds == QStringList{"code"})
        asks << QStringLiteral(
            "coding exercises (type \"code\") based on what the text teaches. In each, the student writes one "
            "function. Give \"language\" (\"python\" unless the text is about JavaScript), \"starter\" (the "
            "function's first line and a body of just pass, or a comment in JavaScript), \"solution\" (a complete, "
            "correct function), and three to five \"tests\": each a \"call\" of the function such as "
            "\"area(2, 3)\" and the \"expected\" value as a literal such as \"6\", \"True\" or \"[1, 2]\". The "
            "question says exactly what the function is called, what it takes, and what it returns. Put a "
            "one-sentence description of the solution in \"answer\"");
    else if (kinds == QStringList{"markov"})
        asks << QStringLiteral(
            "Markov chain exercises (type \"markov\") based on what the text teaches. Each has two to five "
            "\"states\" (short names), the transition \"matrix\" (row i holds the probabilities of moving from "
            "state i to each state, in the same order as the states; each row adds up to 1), and a \"task\": "
            "\"build\" (the question describes the process in words, with every probability, and the student "
            "builds the chain), \"steady\" (the student finds the long-run share of time in each state), or "
            "\"steps\" (the student finds the probabilities after \"steps\" steps, from the \"start\" state, which "
            "is one of the states). Use a mix of tasks. Put the answer in words in \"answer\"");
    else if (kinds == QStringList{"molecule"})
        asks << QStringLiteral(
            "molecule drawing exercises (type \"molecule\") based on what the text teaches: each asks the student "
            "to draw the structure of one molecule or ion named or described in the text (no more than 25 atoms "
            "besides hydrogen). Give its \"smiles\" (a valid SMILES string, hydrogens implied) and its molecular "
            "\"formula\" such as \"C2H6O\". The question names the molecule and only asks the student to draw it; it must not give the structure away or ask anything else. "
            "Put the molecule's name and a short description of its structure in \"answer\"");
    else if (kinds == QStringList{"scenario"})
        asks << QStringLiteral(
            "scenario exercises (type \"scenario\") built on a formula from the text. Each has \"variables\" (the "
            "inputs: a short \"name\" usable in a formula such as \"v\" or \"rate\", a \"label\", a \"unit\" or "
            "\"\", a sensible \"min\" and \"max\", and the \"value\" used in the question) and \"outputs\" (each "
            "with a \"name\", \"label\", \"unit\", and a \"formula\" in the variables and earlier outputs, written "
            "with + - * / ^ and functions such as sqrt, exp, ln, log10, sin, cos, sind, cosd, max, min, normcdf; "
            "angles in degrees use sind and cosd). Set \"task\": \"predict\" (the student works out the output named "
            "in \"output\" for the given values; the question states every value) or \"find\" (the student moves "
            "the variable named in \"vary\" to make \"output\" reach \"target\" when \"goal\" is \"equal\", or "
            "as large or small as it can be when \"goal\" is \"max\" or \"min\"). In \"result\", give the "
            "number the student should get. Put the worked answer in \"answer\"");
    const QString prompt = QStringLiteral(
        "Book: %1\n\nText of the part being tested:\n\"\"\"\n%2\n\"\"\"\n\nWrite %3 %4 that test understanding of "
        "the most important ideas in this text, not trivia. For a multiple-choice question, put the exact text of "
        "the right choice in \"answer\" and make the wrong choices plausible. For a free-response question, put a "
        "model answer in \"answer\". In \"why\", explain the answer in one or two sentences from the text.")
        .arg(m_title, m_text).arg(count).arg(asks.value(0));
    QPointer<PracticeTest> self(this);
    shared->ask(systemPrompt, prompt, questionsSchema(kinds), [self, generation, then, before](const QString &answer,
                                                                                         const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (qEnvironmentVariableIsSet("SHELF_DEBUG_AI"))
            qInfo().noquote() << "practice test:" << answer;
        QVariantList written = before;
        if (error.isEmpty())
            written += parseQuestions(answer.toUtf8());
        if (then && error.isEmpty())
            return then(written);
        self->finish(generation, written, error);
    });
}

void PracticeTest::finish(int generation, QVariantList questions, const QString &error) {
    if (generation != m_generation)
        return;
    // Models like to put the right choice first; shuffle them.
    for (QVariant &v : questions) {
        QVariantMap q = v.toMap();
        if (q.value("type").toString() != "choice")
            continue;
        QStringList choices = q.value("choices").toStringList();
        const QString right = choices.value(q.value("answer").toInt());
        for (qsizetype i = choices.size() - 1; i > 0; --i)
            choices.swapItemsAt(i, QRandomGenerator::global()->bounded(int(i + 1)));
        q["choices"] = choices;
        q["answer"] = int(choices.indexOf(right));
        v = q;
    }
    m_questions = questions;
    if (!error.isEmpty() && questions.isEmpty()) {
        m_state = "failed";
        m_error = error;
    } else if (questions.isEmpty()) {
        m_state = "failed";
        m_error = tr("The model did not write usable questions. Try again, or another model.");
    } else {
        m_state = "ready";
    }
    emit changed();
}

void PracticeTest::grade(int index, const QString &answer) {
    if (!shared || index < 0 || index >= m_questions.size())
        return;
    if (isNonAnswer(answer)) {
        emit graded(index, false, QString(), QString());
        return;
    }
    const QVariantMap q = m_questions[index].toMap();
    const QString prompt = QStringLiteral(
        "Text the question was written from:\n\"\"\"\n%1\n\"\"\"\n\nQuestion: %2\nModel answer: %3\n"
        "Student's answer: \"%4\"\n\n"
        "First, in \"right\", copy word for word the part of the student's answer that is correct. If no part of "
        "it is correct, or it does not answer the question, leave \"right\" empty. Never put anything in "
        "\"right\" that the student did not write. Then set \"correct\": true only if the student's answer gives "
        "the key idea of the model answer, in any wording; false if it is wrong, vague, or misses the key idea.")
        .arg(m_text.left(6000), q.value("question").toString(), q.value("answer").toString(), answer);
    const int generation = m_generation;
    QPointer<PracticeTest> self(this);
    const QString modelAnswer = q.value("answer").toString();
    shared->ask(systemPrompt, prompt, verdictSchema(),
                [self, index, generation, answer, modelAnswer](const QString &reply, const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (!error.isEmpty()) {
            emit self->graded(index, false, QString(), error);
            return;
        }
        const QJsonObject v = QJsonDocument::fromJson(reply.toUtf8()).object();
        const QString credit = groundedCredit(v.value("right").toString(), answer);
        // A right answer shares something with the model answer; one that
        // shares nothing was marked right by mistake.
        bool correct = v.value("correct").toBool();
        if (correct) {
            const QStringList said = contentWords(answer);
            bool overlap = false;
            for (const QString &w : contentWords(modelAnswer))
                overlap = overlap || hasWord(said, w);
            correct = overlap;
        }
        emit self->graded(index, correct, credit, QString());
    });
}

void PracticeTest::reset() {
    ++m_generation;
    m_state.clear();
    m_error.clear();
    m_note.clear();
    m_questions.clear();
    emit changed();
}
