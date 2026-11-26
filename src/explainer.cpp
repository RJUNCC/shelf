#include "explainer.h"

#include "ollama.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace {

const QString systemPrompt = QStringLiteral(
    "You make short animated lessons that explain course material to a student, like a good tutor at a "
    "whiteboard. Each scene has narration, spoken aloud, and a visual that builds while it is spoken. Be "
    "accurate: base everything on the material given, and never invent facts, numbers or quotations. Answer "
    "only with JSON in the requested shape.");

QJsonObject stringArray(int min, int max) {
    return {{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", min}, {"maxItems", max}};
}

// One shape per kind of scene, every field required: models leave out
// fields that are only allowed.
QJsonObject scene(const QString &kind, const QJsonObject &fields) {
    QJsonObject properties{{"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{kind}}}},
                           {"heading", QJsonObject{{"type", "string"}}},
                           {"say", QJsonObject{{"type", "string"}}}};
    QJsonArray required{"kind", "heading", "say"};
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        properties.insert(it.key(), it.value());
        required.append(it.key());
    }
    return {{"type", "object"}, {"properties", properties}, {"required", required}};
}

// KaTeX (with chemistry) and the formula evaluator, run here without a
// page so a lesson is checked before it's shown.
QJSEngine *scripts() {
    static QJSEngine *engine = nullptr;
    if (engine)
        return engine;
    // KaTeX's minified code trips the engine's style warnings; they mean
    // nothing here.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.usedbeforedeclared=false"));
    engine = new QJSEngine(QCoreApplication::instance());
    engine->globalObject().setProperty("self", engine->globalObject());
    for (const char *file : {":/web/vendor/katex/katex.min.js", ":/web/vendor/katex/mhchem.min.js", ":/web/tools/expr.js"}) {
        QFile f(QString::fromLatin1(file));
        if (f.open(QIODevice::ReadOnly)) {
            const QJSValue result = engine->evaluate(QString::fromUtf8(f.readAll()), QString::fromLatin1(file));
            if (result.isError())
                qWarning("explainer: %s: %s", file, qPrintable(result.toString()));
        }
    }
    engine->evaluate(QStringLiteral(
        "function checkTex(tex) { try { katex.renderToString(tex, {throwOnError: true, strict: 'ignore'}); return true }"
        " catch (e) { return false } }\n"
        "function checkGraph(formula, variable, from, to) { try { const node = Expr.parse(formula); let ok = 0;"
        " for (let i = 0; i <= 8; ++i) { const v = Expr.evaluate(node, {[variable]: from + (to - from) * i / 8});"
        " if (isFinite(v)) ++ok } return ok >= 3 } catch (e) { return false } }"));
    return engine;
}

bool texIsFine(const QString &tex) {
    return scripts()->globalObject().property("checkTex").call({tex}).toBool();
}

QStringList strings(const QJsonValue &value, int limit = 6) {
    QStringList out;
    for (const QJsonValue &v : value.toArray()) {
        const QString s = v.toString().simplified();
        if (!s.isEmpty() && out.size() < limit)
            out << s;
    }
    return out;
}

// Narration is spoken, so it carries no Markdown or LaTeX.
QString spoken(QString text) {
    text.remove(QRegularExpression(QStringLiteral("[*_`#]")));
    text.replace(QRegularExpression(QStringLiteral("\\$+([^$]*)\\$+")), QStringLiteral("\\1"));
    text.replace(QRegularExpression(QStringLiteral("\\\\[a-zA-Z]+\\{([^}]*)\\}")), QStringLiteral("\\1"));
    return text.simplified();
}

// Mermaid fails on brackets inside labels, as in A[Glucose (C6)]; quoting
// every plain label avoids that.
QString quoteLabels(QString source) {
    if (!source.startsWith("flowchart") && !source.startsWith("graph"))
        return source;
    static const QRegularExpression square(QStringLiteral("(\\b[A-Za-z_][\\w]*)\\[([^\\]\"\\[]+)\\]"));
    static const QRegularExpression curly(QStringLiteral("(\\b[A-Za-z_][\\w]*)\\{([^}\"{]+)\\}"));
    source.replace(square, QStringLiteral("\\1[\"\\2\"]"));
    source.replace(curly, QStringLiteral("\\1{\"\\2\"}"));
    return source;
}

} // namespace

Explainer::Explainer(Ollama *ollama, const QString &cacheDirectory, QObject *parent)
    : QObject(parent), m_ollama(ollama),
      m_cache(cacheDirectory.isEmpty()
                  ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/shelf/lessons"
                  : cacheDirectory) {}

QJsonObject Explainer::lessonSchema() {
    const QJsonObject string{{"type", "string"}}, number{{"type", "number"}};
    const QJsonObject term{{"type", "object"},
                           {"properties", QJsonObject{{"symbol", string}, {"meaning", string}}},
                           {"required", QJsonArray{"symbol", "meaning"}}};
    const QJsonArray kinds{
        scene("points", {{"points", stringArray(2, 5)}}),
        scene("steps", {{"steps", stringArray(2, 6)}}),
        scene("diagram", {{"mermaid", string}}),
        scene("formula", {{"latex", string},
                          {"terms", QJsonObject{{"type", "array"}, {"items", term}, {"minItems", 1}, {"maxItems", 5}}}}),
        scene("graph", {{"formula", string}, {"variable", string}, {"from", number}, {"to", number},
                        {"xLabel", string}, {"yLabel", string}}),
        scene("bars", {{"labels", stringArray(2, 6)},
                       {"values", QJsonObject{{"type", "array"}, {"items", number}, {"minItems", 2}, {"maxItems", 6}}},
                       {"unit", string}}),
        scene("compare", {{"leftTitle", string}, {"left", stringArray(1, 4)}, {"rightTitle", string},
                          {"right", stringArray(1, 4)}}),
        scene("quote", {{"quote", string}, {"source", string}}),
    };
    return {{"type", "object"},
            {"properties", QJsonObject{{"title", string},
                                       {"scenes", QJsonObject{{"type", "array"},
                                                              {"items", QJsonObject{{"anyOf", kinds}}},
                                                              {"minItems", 2}, {"maxItems", 7}}}}},
            {"required", QJsonArray{"title", "scenes"}}};
}

QString Explainer::prompt(const QVariantMap &input) {
    QString out;
    if (!input.value("title").toString().isEmpty())
        out += QStringLiteral("Topic: %1\n\n").arg(input.value("title").toString());
    out += QStringLiteral("The material:\n\"\"\"\n%1\n\"\"\"\n\n").arg(input.value("text").toString().left(3000));
    if (!input.value("notes").toString().trimmed().isEmpty())
        out += QStringLiteral("The lecturer's notes for it:\n\"\"\"\n%1\n\"\"\"\n\n").arg(input.value("notes").toString().left(1500));
    const QVariantList passages = input.value("passages").toList();
    if (!passages.isEmpty()) {
        out += "What the textbooks say about it:\n\n";
        int used = 0;
        for (const QVariant &v : passages) {
            const QVariantMap p = v.toMap();
            const QString text = p.value("text").toString().left(1800);
            if (used + text.size() > 5000)
                break;
            used += text.size();
            out += QStringLiteral("(%1)\n\"\"\"\n%2\n\"\"\"\n\n").arg(p.value("label").toString(), text);
        }
    }
    const QString question = input.value("question").toString().trimmed();
    if (!question.isEmpty())
        out += QStringLiteral("The student asks: %1\n\nAnswer that question as a lesson of two to four scenes.\n\n").arg(question);
    else
        out += "Explain this as a lesson of four to six scenes that builds understanding step by step: what it is, "
               "how it works, why it matters, and an example.\n\n";
    out += "For each scene:\n"
           "- \"say\": what the tutor says aloud while drawing, two to four short, friendly sentences. Talk about "
           "the picture as it appears, like a teacher at a whiteboard: say what you're drawing and point at its "
           "parts (\"First I'll draw glucose here on the left. This arrow is glycolysis, and it leads to two "
           "pyruvates.\", \"Look at the bottom of the fraction: that's time.\", \"See how the curve gets steeper?\"). "
           "Mention the parts in the order they're drawn. It is spoken, so write symbols and formulas as words (say "
           "\"v naught\", not v_0) and use no Markdown.\n"
           "- \"heading\": a title of a few words.\n"
           "- the visual, which must be short: points and steps of at most eight words each.\n"
           "Choose the kind of each visual by what it shows:\n"
           "- \"diagram\" for a process, cycle, sequence, cause and effect, or parts of a whole: a Mermaid flowchart "
           "such as flowchart LR with short labels in quotes, like A[\"Glucose\"] --> B[\"Pyruvate\"].\n"
           "- \"formula\" for an equation: \"latex\" holds it in LaTeX (chemistry with \\ce{...}), \"terms\" what each "
           "symbol means, the symbols in LaTeX too.\n"
           "- \"graph\" when one quantity depends on another: \"formula\" uses + - * / ^ and functions such as sqrt, "
           "exp, ln, sin, in the one \"variable\" from \"from\" to \"to\".\n"
           "- \"bars\" to compare a few numbers from the material.\n"
           "- \"compare\" for two things side by side.\n"
           "- \"steps\" for an ordered procedure; \"points\" for a short list.\n"
           "- \"quote\" for one important sentence copied exactly from the textbooks, with its section in \"source\".\n"
           "Teach with pictures: at least three scenes must be a diagram, formula, graph, bars or compare, and at "
           "most one may be points. Diagrams should have three to eight boxes, with each arrow labelled by what "
           "happens, like A[\"Ethanol\"] -->|\"oxidized\"| B[\"Acetaldehyde\"]. A formula's terms should explain "
           "every symbol, in the order you mention them.";
    return out;
}

QStringList Explainer::sentences(const QString &text) {
    QStringList out;
    // A full stop ends a sentence unless it follows a number or a short
    // abbreviation ("e.g.", "Fig.", "2.5").
    static const QRegularExpression end(QStringLiteral("(?<=[.!?])\\s+(?=[A-Z0-9\"“(])"));
    QString pending;
    for (const QString &piece : text.simplified().split(end, Qt::SkipEmptyParts)) {
        pending += (pending.isEmpty() ? QString() : QStringLiteral(" ")) + piece;
        static const QRegularExpression abbreviation(QStringLiteral("\\b(e\\.g|i\\.e|etc|vs|Fig|Dr|Mr|Mrs|approx)\\.$"),
                                                     QRegularExpression::CaseInsensitiveOption);
        if (abbreviation.match(pending).hasMatch())
            continue;
        out << pending;
        pending.clear();
    }
    if (!pending.isEmpty())
        out << pending;
    return out;
}

QVariantMap Explainer::parseLesson(const QByteArray &json) {
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QVariantList scenes;
    for (const QJsonValue &v : root.value("scenes").toArray()) {
        const QJsonObject s = v.toObject();
        const QString say = spoken(Ollama::repairLatex(s.value("say").toString()));
        if (say.isEmpty())
            continue;
        QString kind = s.value("kind").toString();
        QVariantMap out{{"heading", s.value("heading").toString().simplified()}, {"say", say},
                        {"sentences", sentences(say)}};
        // Points to fall back on when a visual can't be drawn: the narration.
        const auto fallback = [&]() {
            kind = "points";
            QStringList points = strings(s.value("points"));
            if (points.isEmpty())
                points = strings(s.value("steps"));
            if (points.isEmpty())
                points = sentences(say).mid(0, 4);
            out["points"] = points;
        };
        if (kind == "points" || kind == "steps") {
            const QStringList items = strings(s.value(kind));
            if (items.isEmpty())
                fallback();
            else
                out[kind] = items;
        } else if (kind == "diagram") {
            const QString source = quoteLabels(s.value("mermaid").toString().trimmed());
            static const QRegularExpression known(QStringLiteral(
                "^(flowchart|graph|sequenceDiagram|stateDiagram(-v2)?|mindmap|timeline|classDiagram|pie|erDiagram|journey)\\b"));
            if (!known.match(source).hasMatch() || source.count('\n') < 1)
                fallback();
            else
                out["mermaid"] = source;
        } else if (kind == "formula") {
            const QString latex = Ollama::repairLatex(s.value("latex").toString()).trimmed().remove(QRegularExpression("^\\$+|\\$+$"));
            QVariantList terms;
            for (const QJsonValue &t : s.value("terms").toArray()) {
                const QString symbol = Ollama::repairLatex(t.toObject().value("symbol").toString()).trimmed()
                                           .remove(QRegularExpression("^\\$+|\\$+$"));
                const QString meaning = t.toObject().value("meaning").toString().simplified();
                if (!symbol.isEmpty() && !meaning.isEmpty() && texIsFine(symbol))
                    terms << QVariantMap{{"symbol", symbol}, {"meaning", meaning}};
            }
            if (latex.isEmpty() || !texIsFine(latex)) {
                fallback();
            } else {
                out["latex"] = latex;
                out["terms"] = terms;
            }
        } else if (kind == "graph") {
            const QString formula = s.value("formula").toString().trimmed();
            const QString variable = s.value("variable").toString().trimmed();
            double from = s.value("from").toDouble(), to = s.value("to").toDouble();
            if (to < from)
                std::swap(from, to);
            const bool fine = !formula.isEmpty() && !variable.isEmpty() && to > from
                              && scripts()->globalObject().property("checkGraph").call({formula, variable, from, to}).toBool();
            if (!fine) {
                fallback();
            } else {
                out["formula"] = formula;
                out["variable"] = variable;
                out["from"] = from;
                out["to"] = to;
                out["xLabel"] = s.value("xLabel").toString();
                out["yLabel"] = s.value("yLabel").toString();
            }
        } else if (kind == "bars") {
            const QStringList labels = strings(s.value("labels"));
            QVariantList values;
            for (const QJsonValue &n : s.value("values").toArray())
                if (n.isDouble())
                    values << n.toDouble();
            if (labels.size() < 2 || labels.size() != values.size()) {
                fallback();
            } else {
                out["labels"] = labels;
                out["values"] = values;
                out["unit"] = s.value("unit").toString();
            }
        } else if (kind == "compare") {
            const QStringList left = strings(s.value("left"), 4), right = strings(s.value("right"), 4);
            if (left.isEmpty() || right.isEmpty()) {
                fallback();
            } else {
                out["leftTitle"] = s.value("leftTitle").toString().simplified();
                out["rightTitle"] = s.value("rightTitle").toString().simplified();
                out["left"] = left;
                out["right"] = right;
            }
        } else if (kind == "quote") {
            const QString quote = s.value("quote").toString().simplified();
            if (quote.isEmpty()) {
                fallback();
            } else {
                out["quote"] = quote;
                out["source"] = s.value("source").toString().simplified();
            }
        } else {
            fallback();
        }
        out["kind"] = kind;
        scenes << out;
    }
    if (scenes.isEmpty())
        return {};
    return {{"title", root.value("title").toString().simplified()}, {"scenes", scenes}};
}

QString Explainer::cacheFile(const QVariantMap &input) const {
    // The key and everything the lesson is made from, so a changed slide or
    // new links make a new lesson.
    // The passages themselves are left out: they're fetched again each time
    // and the links they come from ("sources") stand for them.
    QVariantMap identity = input;
    identity.remove("passages");
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QJsonDocument(QJsonObject::fromVariantMap(identity)).toJson(QJsonDocument::Compact));
    return m_cache + "/" + QString::fromLatin1(hash.result().toHex()) + ".json";
}

QVariantMap Explainer::cached(const QVariantMap &input) const {
    QFile file(cacheFile(input));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

void Explainer::explain(const QVariantMap &input) {
    const QString key = input.value("key").toString();
    const QVariantMap known = cached(input);
    if (!known.isEmpty()) {
        emit ready(key, known);
        return;
    }
    if (!m_ollama) {
        m_error = tr("Local AI is not available.");
        emit changed();
        return;
    }
    m_pending = key;
    m_error.clear();
    emit changed();
    const int generation = ++m_generation;
    QPointer<Explainer> self(this);
    const QString file = cacheFile(input);
    const auto handle = [self, generation, key, file](const QString &answer, const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (qEnvironmentVariableIsSet("SHELF_DEBUG_AI"))
            qInfo().noquote() << "explainer:" << answer;
        self->m_pending.clear();
        const QVariantMap lesson = error.isEmpty() ? parseLesson(answer.toUtf8()) : QVariantMap{};
        if (lesson.isEmpty()) {
            self->m_error = !error.isEmpty() ? error : tr("The AI's lesson couldn't be used. Try again.");
            emit self->changed();
            return;
        }
        QDir().mkpath(QFileInfo(file).absolutePath());
        QSaveFile out(file);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(QJsonObject::fromVariantMap(lesson)).toJson(QJsonDocument::Compact));
            out.commit();
        }
        emit self->changed();
        emit self->ready(key, lesson);
    };
    // A file of a lesson stands in for the model, for trying the player.
    if (const QString fake = qEnvironmentVariable("SHELF_LESSON_FILE"); !fake.isEmpty()) {
        QFile f(fake);
        const QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        QMetaObject::invokeMethod(this, [handle, text]() { handle(text, {}); }, Qt::QueuedConnection);
        return;
    }
    m_ollama->ask(systemPrompt, prompt(input), lessonSchema(), handle);
}

void Explainer::cancel() {
    if (m_pending.isEmpty())
        return;
    ++m_generation;
    m_pending.clear();
    emit changed();
}

QVariantList Explainer::parseTopics(const QByteArray &json) {
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value("topics").toArray()) {
        const QJsonObject t = v.toObject();
        const QString topic = t.value("topic").toString().simplified();
        if (topic.isEmpty())
            continue;
        QStringList keywords;
        for (const QJsonValue &k : t.value("keywords").toArray())
            if (!k.toString().simplified().isEmpty())
                keywords << k.toString().simplified().toLower();
        out << QVariantMap{{"topic", topic}, {"keywords", keywords}, {"details", t.value("details").toString().simplified()}};
    }
    return out;
}

void Explainer::studyTopics(const QString &key, const QString &review) {
    if (!m_ollama) {
        emit topicsFailed(key, tr("Local AI is not available."));
        return;
    }
    const QJsonObject string{{"type", "string"}};
    const QJsonObject topic{
        {"type", "object"},
        {"properties", QJsonObject{{"topic", string}, {"details", string},
                                   {"keywords", QJsonObject{{"type", "array"}, {"items", string}, {"minItems", 2}, {"maxItems", 8}}}}},
        {"required", QJsonArray{"topic", "details", "keywords"}}};
    const QJsonObject schema{
        {"type", "object"},
        {"properties", QJsonObject{{"topics", QJsonObject{{"type", "array"}, {"items", topic}, {"minItems", 1}, {"maxItems", 40}}}}},
        {"required", QJsonArray{"topics"}}};
    const QString prompt = QStringLiteral(
        "Here is a study review from a teacher, listing what the next exam covers:\n\"\"\"\n%1\n\"\"\"\n\n"
        "List every topic it names, in its order, keeping the teacher's wording for \"topic\". In \"details\", say in "
        "one short sentence what to be able to do (from the review only). In \"keywords\", give two to eight words or "
        "short phrases a lecture slide on that topic would contain, including technical terms and their abbreviations.")
        .arg(review.left(8000));
    QPointer<Explainer> self(this);
    m_ollama->ask(QStringLiteral("You help students prepare for exams. Answer only with JSON in the requested shape."),
                  prompt, schema, [self, key](const QString &answer, const QString &error) {
                      if (!self)
                          return;
                      const QVariantList topics = error.isEmpty() ? parseTopics(answer.toUtf8()) : QVariantList{};
                      if (topics.isEmpty())
                          emit self->topicsFailed(key, !error.isEmpty() ? error : tr("No topics could be read from the review."));
                      else
                          emit self->topicsReady(key, topics);
                  });
}
