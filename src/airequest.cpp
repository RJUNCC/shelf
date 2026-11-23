#include "airequest.h"

#include "ollama.h"

#include <QNetworkReply>

namespace {
Ollama *shared = nullptr;

// Long passages are cut, so a whole chapter selected by accident still fits
// the model's context with room to answer.
QString clip(const QString &text, int chars) {
    const QString t = text.simplified();
    return t.size() <= chars ? t : t.left(chars) + "…";
}
} // namespace

void AiRequest::setOllama(Ollama *ollama) { shared = ollama; }

AiRequest::~AiRequest() {
    if (m_reply)
        m_reply->abort();
}

QString AiRequest::systemPrompt() {
    return QStringLiteral(
        "You are a study assistant inside an e-book reader, helping a student understand what they are "
        "reading. Answer in clear, plain English using Markdown: short paragraphs, bullet points where they "
        "help, bold for key terms. Be accurate and concise. Base your answer on the passage; if you add "
        "something the passage does not say, make that clear. Never invent page numbers or quotations.");
}

QString AiRequest::prompt(const QString &action, const QVariantMap &input) {
    // A question about the whole book, answered from the passages found
    // for it, each numbered so the answer can cite them.
    if (action == "book") {
        QString out;
        if (!input.value("title").toString().isEmpty())
            out += QStringLiteral("Book: %1\n\n").arg(input.value("title").toString());
        out += "Passages from the book that may answer the question:\n\n";
        for (const QVariant &v : input.value("passages").toList()) {
            const QVariantMap p = v.toMap();
            out += QStringLiteral("[%1] (%2)\n\"\"\"\n%3\n\"\"\"\n\n")
                       .arg(p.value("n").toInt()).arg(p.value("label").toString(), clip(p.value("text").toString(), 1800));
        }
        out += QStringLiteral(
                   "Question: %1\n\nAnswer from these passages only. After each claim, cite the passage it comes "
                   "from as [1], [2], and so on. If the passages do not answer the question, say so plainly "
                   "instead of guessing.").arg(input.value("question").toString());
        return out;
    }
    const QString text = clip(input.value("text").toString(), 4000);
    const QString context = clip(input.value("context").toString(), 3000);
    const QString title = input.value("title").toString();
    QString out;
    if (!title.isEmpty())
        out += QStringLiteral("Book: %1\n\n").arg(title);
    if (!context.isEmpty() && context != text)
        out += QStringLiteral("The text around the passage:\n\"\"\"\n%1\n\"\"\"\n\n").arg(context);
    out += QStringLiteral("The passage:\n\"\"\"\n%1\n\"\"\"\n\n").arg(text);
    if (action == "summarize")
        out += "Summarize the passage in three to five bullet points, keeping the key terms.";
    else if (action == "quiz")
        out += "Write three flashcards that test the most important facts or ideas in the passage. Format "
               "each as a line starting with **Q:** and the next line starting with **A:**, with a blank "
               "line between cards. Keep answers short.";
    else if (action == "ask")
        out += QStringLiteral("Answer this question about the passage: %1").arg(input.value("question").toString());
    else
        out += "Explain the passage so a student understands it: what it means, the key terms in it, and "
               "why it matters. Add a short example if it helps.";
    return out;
}

void AiRequest::run(const QString &action, const QVariantMap &input) {
    cancel();
    m_action = action;
    m_answer.clear();
    m_error.clear();
    if (!shared) {
        m_error = tr("Local AI is not available.");
        emit changed();
        return;
    }
    m_running = true;
    emit changed();
    // Answers from a cancelled request that arrive late are ignored.
    const int generation = ++m_generation;
    m_reply = shared->stream(systemPrompt(), prompt(action, input),
        [this, generation](const QString &piece) {
            if (generation != m_generation)
                return;
            m_answer += piece;
            emit changed();
        },
        [this, generation](const QString &answer, const QString &error) {
            if (generation != m_generation)
                return;
            m_running = false;
            m_answer = answer;
            m_error = error;
            m_reply = nullptr;
            emit changed();
            emit finished();
        });
    if (!m_reply && m_running) {
        // stream() answered at once (no model); `done` already ran.
        m_running = false;
        emit changed();
    }
}

void AiRequest::cancel() {
    if (m_reply)
        m_reply->abort();
    m_reply = nullptr;
    if (m_running) {
        m_running = false;
        ++m_generation;
        emit changed();
    }
}

void AiRequest::clear() {
    cancel();
    m_answer.clear();
    m_error.clear();
    m_action.clear();
    emit changed();
}
