QT += core gui qml quick quickcontrols2 network pdf svg webenginecore webenginequick webchannel

CONFIG += c++17 release

# One place for the version and the GitHub repository the updater checks
# (pkgbuild/PKGBUILD and windows/shelf.iss carry the version too).
VERSION = 0.1.0
SHELF_REPO = RJUNCC/shelf
DEFINES += SHELF_VERSION=\\\"$$VERSION\\\" SHELF_REPO=\\\"$$SHELF_REPO\\\"
TARGET = shelf
TEMPLATE = app

HEADERS += \
    src/annotations.h \
    src/cards.h \
    src/booksearch.h \
    src/practicetest.h \
    src/knowledgegraph.h \
    src/updater.h \
    src/bookmeta.h \
    src/filepicker.h \
    src/airequest.h \
    src/booktext.h \
    src/cleanpageprovider.h \
    src/iconprovider.h \
    src/ollama.h \
    src/outlines.h \
    src/pdfgeometry.h \
    src/reflow.h \
    src/textversions.h \
    src/zipwriter.h \
    src/library.h \
    src/readerbridge.h \
    src/schemehandler.h \
    src/singleinstance.h \
    src/theme.h \
    src/translator.h \
    src/zipreader.h

SOURCES += \
    src/main.cpp \
    src/annotations.cpp \
    src/cards.cpp \
    src/booksearch.cpp \
    src/practicetest.cpp \
    src/knowledgegraph.cpp \
    src/updater.cpp \
    src/bookmeta.cpp \
    src/airequest.cpp \
    src/booktext.cpp \
    src/cleanpageprovider.cpp \
    src/iconprovider.cpp \
    src/ollama.cpp \
    src/outlines.cpp \
    src/pdfgeometry.cpp \
    src/reflow.cpp \
    src/textversions.cpp \
    src/zipwriter.cpp \
    src/library.cpp \
    src/schemehandler.cpp \
    src/singleinstance.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/zipreader.cpp

RESOURCES += src/resources.qrc src/web.qrc

# Linux asks for files through xdg-desktop-portal over D-Bus; Windows uses
# its own Open dialog.
unix {
    QT += dbus
    HEADERS += src/portalfilepicker.h
    SOURCES += src/portalfilepicker.cpp
    LIBS += -lz
}
win32 {
    HEADERS += src/windowsfilepicker.h
    SOURCES += src/windowsfilepicker.cpp
    LIBS += -lole32 -lshell32 -luuid
    # windows.h would otherwise define min and max as macros.
    DEFINES += NOMINMAX
    # zlib from vcpkg (preinstalled on GitHub's Windows runners).
    VCPKG = $$(VCPKG_INSTALLATION_ROOT)
    isEmpty(VCPKG): VCPKG = C:/vcpkg
    INCLUDEPATH += $$VCPKG/installed/x64-windows/include
    # The library is zlib.lib up to zlib 1.3 and z.lib from 1.4.
    ZLIB_DIR = $$VCPKG/installed/x64-windows/lib
    exists($$ZLIB_DIR/zlib.lib): LIBS += -L$$ZLIB_DIR -lzlib
    else: LIBS += -L$$ZLIB_DIR -lz
    RC_ICONS = windows/shelf.ico
    QMAKE_TARGET_DESCRIPTION = Shelf
    QMAKE_TARGET_PRODUCT = Shelf
}
