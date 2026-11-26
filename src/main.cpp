// shelf — an organized library and reader for EPUB and PDF books.

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWebEngineProfile>
#include <QQuickWindow>
#include <QUrl>
#include <QtWebEngineQuick>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "annotations.h"
#include "cards.h"
#include "knowledgegraph.h"
#include "workspaces.h"
#include "explainer.h"
#include "voice.h"
#include "updater.h"
#include "booksearch.h"
#include "practicetest.h"
#include "iconprovider.h"
#include "airequest.h"
#include "cleanpageprovider.h"
#include "ollama.h"
#include "outlines.h"
#include "pdfgeometry.h"
#include "textversions.h"
#include "library.h"
#ifdef Q_OS_WIN
#include "windowsfilepicker.h"
#else
#include "portalfilepicker.h"
#endif
#include "readerbridge.h"
#include "schemehandler.h"
#include "singleinstance.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Both must happen before the application object exists.
    SchemeHandler::registerScheme();
    QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    app.setApplicationName("shelf");
    app.setApplicationVersion(SHELF_VERSION);

    // Books from the command line (the file manager passes them here), made
    // absolute now because a running instance has a different working dir.
    // Read from arguments() so names in any script survive on Windows too.
    QStringList files;
    for (const QString &arg : app.arguments().mid(1)) {
        if (arg.startsWith('-'))
            continue;
        const QUrl url(arg);
        files << QFileInfo(url.isLocalFile() ? url.toLocalFile() : arg).absoluteFilePath();
    }

    // Matches the window to shelf.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("shelf");
    app.setWindowIcon(QIcon::fromTheme("shelf"));

#ifdef Q_OS_WIN
    // One Shelf per user: the pipe name is per session on Windows already,
    // but keep users of one machine apart anyway.
    SingleInstance instance("shelf-" + qEnvironmentVariable("USERNAME"));
#else
    SingleInstance instance("shelf-" + QString::number(getuid()));
#endif
    if (instance.forwardToRunning(files))
        return 0;
    instance.listen();

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
#ifdef Q_OS_WIN
    WindowsFilePicker picker, slidePicker, audioPicker;
#else
    PortalFilePicker picker, slidePicker, audioPicker;
#endif
    Library library({}, {}, &picker);
    Annotations annotations;
    Cards cards;
    BookSearch bookSearch;
    PdfGeometry pdfGeometry;
    // Optional local AI. The chosen model is remembered with the library's
    // settings; Ollama is asked what is installed once the window is up.
    Ollama ollama;
    ollama.setModel(library.setting("aiModel", QString()).toString());
    QObject::connect(&ollama, &Ollama::modelChanged, &library,
                     [&]() { library.setSetting("aiModel", ollama.model()); });
    ollama.setUseGpu(library.setting("aiUseGpu", true).toBool());
    QObject::connect(&ollama, &Ollama::useGpuChanged, &library,
                     [&]() { library.setSetting("aiUseGpu", ollama.useGpu()); });
    Outlines outlines(&ollama);
    TextVersions textVersions;
    AiRequest::setOllama(&ollama);
    PracticeTest::setOllama(&ollama);
    KnowledgeGraph knowledge(&annotations, &ollama);
    Updater updater(&library);
    Workspaces workspaces(&library, &slidePicker);
    Explainer explainer(&ollama);
    Voice voice(&library, {}, &audioPicker);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &library, &Library::flush);

    SchemeHandler scheme(&library);
    QQuickWebEngineProfile::defaultProfile()->installUrlSchemeHandler("shelf", &scheme);

    qmlRegisterType<ReaderBridge>("Shelf", 1, 0, "ReaderBridge");
    qmlRegisterType<AiRequest>("Shelf", 1, 0, "AiRequest");
    qmlRegisterType<PracticeTest>("Shelf", 1, 0, "PracticeTest");

    QQmlApplicationEngine engine;
    engine.addImageProvider("icon", new IconProvider);
    engine.addImageProvider("cleanpage", new CleanPageProvider);
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("annotations", &annotations);
    engine.rootContext()->setContextProperty("cards", &cards);
    engine.rootContext()->setContextProperty("knowledge", &knowledge);
    engine.rootContext()->setContextProperty("updater", &updater);
    engine.rootContext()->setContextProperty("workspaces", &workspaces);
    engine.rootContext()->setContextProperty("explainer", &explainer);
    engine.rootContext()->setContextProperty("voice", &voice);
    engine.rootContext()->setContextProperty("bookSearch", &bookSearch);
    // A finished outline brings OCR text that search should use.
    QObject::connect(&outlines, &Outlines::changed, &bookSearch, &BookSearch::forget);
    engine.rootContext()->setContextProperty("pdfGeometry", &pdfGeometry);
    engine.rootContext()->setContextProperty("ollama", &ollama);
    engine.rootContext()->setContextProperty("outlines", &outlines);
    engine.rootContext()->setContextProperty("textVersions", &textVersions);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&instance, &SingleInstance::filesReceived, &library, [&](const QStringList &paths) {
        for (const auto &path : paths)
            library.openExternal(path);
        if (window) {
            window->show();
            window->raise();
            window->requestActivate();
        }
    });
    for (const auto &path : files)
        library.openExternal(path);

    return app.exec();
}
