#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>
#include <QUrl>

namespace {

// The portal's filter type is a(sa(us)): a name and a list of
// (kind, pattern) pairs, where kind 0 is a glob and 1 a MIME type.
struct FilterRule {
    uint kind = 0;
    QString pattern;
};
struct Filter {
    QString name;
    QList<FilterRule> rules;
};

} // namespace

Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(Filter)

static QDBusArgument &operator<<(QDBusArgument &arg, const FilterRule &rule) {
    arg.beginStructure();
    arg << rule.kind << rule.pattern;
    arg.endStructure();
    return arg;
}
static const QDBusArgument &operator>>(const QDBusArgument &arg, FilterRule &rule) {
    arg.beginStructure();
    arg >> rule.kind >> rule.pattern;
    arg.endStructure();
    return arg;
}
static QDBusArgument &operator<<(QDBusArgument &arg, const Filter &filter) {
    arg.beginStructure();
    arg << filter.name << filter.rules;
    arg.endStructure();
    return arg;
}
static const QDBusArgument &operator>>(const QDBusArgument &arg, Filter &filter) {
    arg.beginStructure();
    arg >> filter.name >> filter.rules;
    arg.endStructure();
    return arg;
}

void PortalFilePicker::pickBooks(const QString &title) {
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<QList<FilterRule>>();
    qDBusRegisterMetaType<Filter>();
    qDBusRegisterMetaType<QList<Filter>>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tr("The file chooser is unavailable: no D-Bus session."));
        return;
    }

    // Subscribe to the request's Response before asking, at the path the
    // portal will use, so a fast answer cannot slip past us.
    const QString token = "shelf" + QString::number(QRandomGenerator::global()->generate());
    const QString sender = bus.baseService().mid(1).replace('.', '_');
    if (!m_requestPath.isEmpty())
        bus.disconnect({}, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(onResponse(uint, QVariantMap)));
    m_requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    bus.connect({}, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                SLOT(onResponse(uint, QVariantMap)));

    const char *pptx = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    const QList<Filter> filters = kind == "documents"
        ? QList<Filter>{
              {tr("Documents"), {{1, "application/pdf"},
                                 {1, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
                                 {1, "application/vnd.oasis.opendocument.text"}, {1, "text/html"}, {1, "text/plain"},
                                 {1, "text/markdown"}, {0, "*.pdf"}, {0, "*.docx"}, {0, "*.odt"}, {0, "*.html"},
                                 {0, "*.htm"}, {0, "*.txt"}, {0, "*.md"}}},
          }
        : kind == "audio"
        ? QList<Filter>{
              {tr("Recordings"), {{1, "audio/wav"}, {1, "audio/x-wav"}, {1, "audio/mpeg"}, {1, "audio/flac"},
                                  {0, "*.wav"}, {0, "*.mp3"}, {0, "*.flac"}}},
          }
        : kind == "slides"
        ? QList<Filter>{
              {tr("Slides"), {{1, "application/pdf"}, {1, pptx}, {0, "*.pdf"}, {0, "*.pptx"}}},
              {tr("PDF"), {{1, "application/pdf"}, {0, "*.pdf"}}},
              {tr("PowerPoint"), {{1, pptx}, {0, "*.pptx"}}},
          }
        : QList<Filter>{
              {tr("Books"), {{1, "application/epub+zip"}, {1, "application/pdf"}, {0, "*.epub"}, {0, "*.pdf"}}},
              {tr("EPUB"), {{1, "application/epub+zip"}, {0, "*.epub"}}},
              {tr("PDF"), {{1, "application/pdf"}, {0, "*.pdf"}}},
          };
    QVariantMap options{{"handle_token", token},
                        {"multiple", kind == "books" || kind == "slides"},
                        {"modal", true},
                        {"filters", QVariant::fromValue(filters)}};

    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",
                                                       "/org/freedesktop/portal/desktop",
                                                       "org.freedesktop.portal.FileChooser", "OpenFile");
    call << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            emit failed(tr("The file chooser is unavailable: %1").arg(reply.error().message()));
            return;
        }
        // Older portals pick their own path; follow it if it differs.
        const QString path = reply.value().path();
        if (path != m_requestPath) {
            QDBusConnection bus = QDBusConnection::sessionBus();
            bus.disconnect({}, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                           SLOT(onResponse(uint, QVariantMap)));
            m_requestPath = path;
            bus.connect({}, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                        SLOT(onResponse(uint, QVariantMap)));
        }
    });
}

void PortalFilePicker::onResponse(uint response, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect({}, m_requestPath, "org.freedesktop.portal.Request", "Response",
                                             this, SLOT(onResponse(uint, QVariantMap)));
    m_requestPath.clear();
    // 1 is "cancelled by the user", which needs no message.
    if (response == 1)
        return;
    if (response != 0) {
        emit failed(tr("The file chooser closed with an error."));
        return;
    }
    QStringList uris = results.value("uris").toStringList();
    if (uris.isEmpty() && results.value("uris").canConvert<QDBusArgument>())
        results.value("uris").value<QDBusArgument>() >> uris;
    QStringList paths;
    for (const auto &uri : uris) {
        const QUrl url(uri);
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    if (!paths.isEmpty())
        emit picked(paths);
}
