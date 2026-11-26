QT += core gui quick quickcontrols2 testlib dbus pdf network qml
CONFIG += c++17 testcase
TARGET = shelf_tests
TEMPLATE = app
LIBS += -lz

INCLUDEPATH += ../src

HEADERS += helpers.h ../src/annotations.h ../src/cards.h ../src/booksearch.h ../src/practicetest.h ../src/knowledgegraph.h ../src/slides.h ../src/explainer.h ../src/voice.h ../src/studiovoice.h ../src/workspaces.h ../src/updater.h ../src/bookmeta.h ../src/filepicker.h ../src/library.h ../src/reflow.h ../src/zipwriter.h ../src/cleanpageprovider.h ../src/airequest.h ../src/booktext.h ../src/ollama.h ../src/outlines.h ../src/pdfgeometry.h ../src/theme.h ../src/translator.h ../src/zipreader.h
SOURCES += shelf_tests.cpp ../src/annotations.cpp ../src/cards.cpp ../src/booksearch.cpp ../src/practicetest.cpp ../src/knowledgegraph.cpp ../src/slides.cpp ../src/explainer.cpp ../src/voice.cpp ../src/studiovoice.cpp ../src/workspaces.cpp ../src/updater.cpp ../src/bookmeta.cpp ../src/library.cpp ../src/reflow.cpp ../src/zipwriter.cpp ../src/cleanpageprovider.cpp ../src/airequest.cpp ../src/booktext.cpp ../src/ollama.cpp ../src/outlines.cpp ../src/pdfgeometry.cpp ../src/theme.cpp ../src/translator.cpp ../src/zipreader.cpp

RESOURCES += ../src/web.qrc

# The QML-side scripts are tested straight from the source tree.
DEFINES += SOURCE_DIR=\\\"$$PWD/../src\\\"
