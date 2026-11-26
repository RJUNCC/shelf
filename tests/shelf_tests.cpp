#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <zlib.h>

#include "airequest.h"
#include "annotations.h"
#include "cards.h"
#include "voice.h"
#include "studiovoice.h"
#include "explainer.h"
#include "slides.h"
#include "workspaces.h"
#include "ollama.h"
#include "booksearch.h"
#include "practicetest.h"
#include "knowledgegraph.h"
#include "updater.h"
#include "booktext.h"
#include "reflow.h"
#include "zipwriter.h"
#include "cleanpageprovider.h"
#include "outlines.h"
#include "pdfgeometry.h"
#include "bookmeta.h"
#include "filepicker.h"
#include "library.h"
#include "theme.h"
#include "translator.h"
#include "zipreader.h"
#include "helpers.h"


class ShelfTests : public QObject {
    Q_OBJECT

private slots:
    void zipReadsStoredAndDeflatedEntries() {
        QTemporaryDir dir;
        for (bool deflate : {false, true}) {
            const QString path = dir.filePath(deflate ? "d.zip" : "s.zip");
            write(path, makeZip({{"a.txt", QByteArray(5000, 'a')}, {"dir/b.txt", "bee"}}, deflate));
            ZipReader zip(path);
            QVERIFY(zip.isValid());
            QCOMPARE(zip.read("a.txt"), QByteArray(5000, 'a'));
            QCOMPARE(zip.read("dir/b.txt"), QByteArray("bee"));
            QVERIFY(zip.read("missing").isEmpty());
        }
    }

    void zipRejectsGarbage() {
        QTemporaryDir dir;
        write(dir.filePath("x.zip"), "PK\x03\x04 not really a zip");
        QVERIFY(!ZipReader(dir.filePath("x.zip")).isValid());
        QVERIFY(!ZipReader(dir.filePath("nope.zip")).isValid());
    }

    void epubMetadataAndEpub3Cover() {
        QTemporaryDir dir;
        const QString path = writeEpub(dir.filePath("book.epub"), "The   Left Hand", "Ursula K. Le Guin");
        QCOMPARE(BookMetaReader::detectFormat(path), QString("epub"));
        const BookMeta meta = BookMetaReader::read(path);
        QVERIFY2(meta.error.isEmpty(), qPrintable(meta.error));
        QCOMPARE(meta.format, QString("epub"));
        QCOMPARE(meta.title, QString("The Left Hand"));
        QCOMPARE(meta.author, QString("Ursula K. Le Guin"));
        QVERIFY(!meta.cover.isNull());
        QCOMPARE(meta.cover.pixelColor(5, 5), QColor(Qt::red));
    }

    void epub2CoverFromMeta() {
        QTemporaryDir dir;
        const QString path = writeEpub(dir.filePath("old.epub"), "Old", "Someone", false, false);
        const BookMeta meta = BookMetaReader::read(path);
        QVERIFY(!meta.cover.isNull());
    }

    void pdfMetadataAndCover() {
        QTemporaryDir dir;
        const QString path = writePdf(dir.filePath("paper.pdf"), "A Paper", "Writer");
        QCOMPARE(BookMetaReader::detectFormat(path), QString("pdf"));
        const BookMeta meta = BookMetaReader::read(path);
        QVERIFY2(meta.error.isEmpty(), qPrintable(meta.error));
        QCOMPARE(meta.title, QString("A Paper"));
        QVERIFY(!meta.cover.isNull());
        QCOMPARE(meta.cover.height(), 480);
    }

    void unknownFilesAreRefused() {
        QTemporaryDir dir;
        write(dir.filePath("notes.txt"), "just text");
        QVERIFY(!BookMetaReader::read(dir.filePath("notes.txt")).error.isEmpty());
        QVERIFY(!BookMetaReader::read(dir.filePath("gone.epub")).error.isEmpty());
    }

    void addingBooksAndDeduplicating() {
        QTemporaryDir dir;
        const QString epub = writeEpub(dir.filePath("b.epub"), "Book", "Author");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        const QString id = library.addFile(epub);
        QVERIFY(!id.isEmpty());
        QCOMPARE(library.addFile(epub), id);
        QCOMPARE(library.totalCount(), 1);
        const QVariantMap book = library.book(id);
        QCOMPARE(book["title"].toString(), QString("Book"));
        QVERIFY(QFile::exists(QUrl(book["cover"].toUrl()).toLocalFile()));
        QCOMPARE(library.readerUrl(id), QUrl("shelf://book/" + id + ".epub"));

        QVERIFY(library.addFile(dir.filePath("missing.epub")).isEmpty());
        QVERIFY(!library.message().isEmpty());
    }

    void titleFallsBackToFileName() {
        QTemporaryDir dir;
        const QString epub = writeEpub(dir.filePath("Untitled Thing.epub"), "", "");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        QCOMPARE(library.book(library.addFile(epub))["title"].toString(), QString("Untitled Thing"));
    }

    void progressAndCollectionsPersist() {
        QTemporaryDir dir;
        const QString epub = writeEpub(dir.filePath("b.epub"), "Book", "Author");
        QString id;
        {
            Library library(dir.filePath("data"), dir.filePath("cache"));
            id = library.addFile(epub);
            library.markOpened(id);
            library.saveProgress(id, "epubcfi(/6/4!/4/2)", 0.42);
            QVERIFY(library.createCollection("Sci-fi"));
            QVERIFY(!library.createCollection("sci-fi"));
            library.setInCollection(id, "Sci-fi", true);
            library.setSort("author");
            library.setSetting("epubFontScale", 120);
        }  // destruction flushes
        Library library(dir.filePath("data"), dir.filePath("cache"));
        const QVariantMap book = library.book(id);
        QCOMPARE(book["location"].toString(), QString("epubcfi(/6/4!/4/2)"));
        QCOMPARE(book["progress"].toDouble(), 0.42);
        QCOMPARE(book["collections"].toStringList(), QStringList{"Sci-fi"});
        QCOMPARE(library.collections(), QStringList{"Sci-fi"});
        QCOMPARE(library.sort(), QString("author"));
        QCOMPARE(library.setting("epubFontScale").toInt(), 120);
        QCOMPARE(library.continueReading().size(), 1);
    }

    void savesWithoutWaitingForExit() {
        QTemporaryDir dir;
        Library library(dir.filePath("data"), dir.filePath("cache"));
        library.addFile(writeEpub(dir.filePath("b.epub"), "Book", "Author"));
        QTRY_VERIFY(QFile::exists(dir.filePath("data/library.json")));
    }

    void shelvesFilterAndSort() {
        QTemporaryDir dir;
        Library library(dir.filePath("data"), dir.filePath("cache"));
        const QString a = library.addFile(writeEpub(dir.filePath("a.epub"), "The Zebra", "Amy Young"));
        const QString b = library.addFile(writeEpub(dir.filePath("b.epub"), "Apple", "Bob Zane"));
        const QString c = library.addFile(writePdf(dir.filePath("c.pdf"), "Middle", "x"));
        auto titles = [&] {
            QStringList out;
            for (const auto &v : library.books())
                out << v.toMap()["title"].toString();
            return out;
        };

        library.setSort("title");
        QCOMPARE(titles(), (QStringList{"Apple", "Middle", "The Zebra"}));  // "The" is ignored
        library.setSort("author");
        QCOMPARE(titles().first(), QString("The Zebra"));  // Young before Zane

        library.setShelf("pdf");
        QCOMPARE(titles(), QStringList{"Middle"});
        library.setShelf("all");
        library.setSearch("zane");
        QCOMPARE(titles(), QStringList{"Apple"});
        library.setSearch("");

        library.saveProgress(a, "x", 0.3);
        library.setFinished(b, true);
        library.setShelf("reading");
        QCOMPARE(titles(), QStringList{"The Zebra"});
        library.setShelf("finished");
        QCOMPARE(titles(), QStringList{"Apple"});

        library.createCollection("Pile");
        library.setInCollection(c, "Pile", true);
        library.setShelf("collection:Pile");
        QCOMPARE(titles(), QStringList{"Middle"});
        library.deleteCollection("Pile");
        QCOMPARE(library.shelf(), QString("all"));
        QVERIFY(library.book(c)["collections"].toStringList().isEmpty());

        library.setSort("recent");
        library.markOpened(c);
        QCOMPARE(titles().first(), QString("Middle"));
    }

    void removingKeepsTheFile() {
        QTemporaryDir dir;
        const QString epub = writeEpub(dir.filePath("b.epub"), "Book", "Author");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        const QString id = library.addFile(epub);
        library.remove(id);
        QCOMPARE(library.totalCount(), 0);
        QVERIFY(QFile::exists(epub));
    }

    void missingFilesAreFlagged() {
        QTemporaryDir dir;
        const QString epub = writeEpub(dir.filePath("b.epub"), "Book", "Author");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        const QString id = library.addFile(epub);
        QFile::remove(epub);
        QVERIFY(library.book(id)["missing"].toBool());
    }

    void foldersAddTheirBooks() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("books/sub");
        writeEpub(dir.filePath("books/one.epub"), "One", "A");
        writePdf(dir.filePath("books/sub/two.pdf"), "Two", "B");
        write(dir.filePath("books/readme.txt"), "ignored");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        QCOMPARE(library.addUrls({QUrl::fromLocalFile(dir.filePath("books"))}), 2);
    }

    void pickerAddsBooks() {
        QTemporaryDir dir;
        FakePicker picker;
        picker.answer = {writeEpub(dir.filePath("p.epub"), "Picked", "A")};
        Library library(dir.filePath("data"), dir.filePath("cache"), &picker);
        library.pickFiles();
        QCOMPARE(library.totalCount(), 1);
    }

    void externalOpenAddsAndRequests() {
        QTemporaryDir dir;
        Library library(dir.filePath("data"), dir.filePath("cache"));
        QSignalSpy spy(&library, &Library::openRequested);
        library.openExternal(writeEpub(dir.filePath("e.epub"), "Ext", "A"));
        QCOMPARE(spy.count(), 1);
    }

    void brokenLibraryFileIsSetAside() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("data"));
        write(dir.filePath("data/library.json"), "{ not json");
        Library library(dir.filePath("data"), dir.filePath("cache"));
        QCOMPARE(library.totalCount(), 0);
        QVERIFY(!library.message().isEmpty());
        const QStringList kept = QDir(dir.filePath("data")).entryList({"library.json.broken-*"});
        QCOMPARE(kept.size(), 1);
    }

    void annotationsAddUpdateRemove() {
        QTemporaryDir dir;
        const QString book = "abc123";
        QString first, second;
        {
            Annotations a(dir.path());
            second = a.add(book, {{"kind", "highlight"}, {"location", "epubcfi(/6/8)"}, {"position", 0.6},
                                  {"text", "Later passage"}, {"color", "green"}, {"chapter", "Two"}});
            first = a.add(book, {{"kind", "highlight"}, {"location", "epubcfi(/6/2)"}, {"position", 0.1},
                                 {"text", "Early passage"}, {"color", "purple"}, {"chapter", "One"}});
            QVERIFY(!first.isEmpty());
            QCOMPARE(a.count(book), 2);
            // Reading order, not creation order; unknown colors fall back.
            QCOMPARE(a.list(book).first().toMap()["id"].toString(), first);
            QCOMPARE(a.get(book, first)["color"].toString(), QString("yellow"));
            a.update(book, first, {{"note", "Remember this"}, {"location", "tampered"}});
            QCOMPARE(a.get(book, first)["note"].toString(), QString("Remember this"));
            QCOMPARE(a.get(book, first)["location"].toString(), QString("epubcfi(/6/2)"));
        }
        Annotations reloaded(dir.path());
        QCOMPARE(reloaded.count(book), 2);
        reloaded.remove(book, second);
        QCOMPARE(reloaded.count(book), 1);
        QCOMPARE(reloaded.get(book, first)["note"].toString(), QString("Remember this"));
    }

    void libraryActsOnManyBooksAtOnce() {
        QTemporaryDir dir;
        FakePicker picker;
        Library library(dir.filePath("data"), dir.filePath("cache"), &picker);
        QStringList ids;
        for (const QString &name : {"a", "b", "c"})
            ids << library.addFile(writePdf(dir.filePath(name + ".pdf"), name.toUpper(), "Someone"));
        QCOMPARE(ids.size(), 3);
        QSignalSpy views(&library, &Library::viewChanged);
        // A new collection, made on the way, gets all three in one change.
        QCOMPARE(library.addToCollection(ids, " Study "), 3);
        QCOMPARE(views.count(), 1);
        QVERIFY(library.collections().contains("Study"));
        // Adding again, in another case, adds nothing and makes no new one.
        QCOMPARE(library.addToCollection(ids.mid(0, 2), "study"), 0);
        QCOMPARE(library.collections().size(), 1);
        library.setFinishedMany(ids.mid(1), true);
        QVERIFY(library.book(ids[2])["finished"].toBool());
        QVERIFY(!library.book(ids[0])["finished"].toBool());
        library.removeMany({ids[0], ids[1], "nonsense"});
        QCOMPARE(library.totalCount(), 1);
        QVERIFY(library.book(ids[2])["collections"].toStringList().contains("Study"));
    }

    void annotationsUndoRedo() {
        QTemporaryDir dir;
        Annotations a(dir.path());
        const QString book = "abc123";
        const QString mark = a.add(book, {{"kind", "bookmark"}, {"location", "12"}, {"position", 0.2}});
        a.update(book, mark, {{"note", "Come back"}});
        a.remove(book, mark);
        QCOMPARE(a.count(book), 0);
        QVERIFY(!a.canRedo(book));

        // Undoing the removal brings the bookmark back with its note.
        QCOMPARE(a.undo(book)["action"].toString(), QString("remove"));
        QCOMPARE(a.get(book, mark)["note"].toString(), QString("Come back"));
        QCOMPARE(a.undo(book)["action"].toString(), QString("update"));
        QVERIFY(a.get(book, mark)["note"].toString().isEmpty());
        QCOMPARE(a.undo(book)["kind"].toString(), QString("bookmark"));
        QCOMPARE(a.count(book), 0);
        QVERIFY(!a.canUndo(book));
        QVERIFY(a.undo(book).isEmpty());

        QCOMPARE(a.redo(book)["action"].toString(), QString("add"));
        QCOMPARE(a.count(book), 1);
        // A new change after undoing drops what could have been redone.
        a.add(book, {{"kind", "bookmark"}, {"location", "40"}, {"position", 0.7}});
        QVERIFY(!a.canRedo(book));
        QCOMPARE(a.count(book), 2);
        // Other books keep their own history.
        QVERIFY(!a.canUndo("def456"));
    }

    void bookTextFindsPageNumbersAndContents() {
        QStringList pages{"Title page", "Copyright 2024",
                          "Table of Contents\nPart 1: Basics\nChapter 1: Rivers ........ 5\n"
                          "Chapter 2: Mountains .... 15\nChapter 3: Deserts .. 30\nIndex 99\n"};
        // Printed numbers run two behind the page indexes, in the footer.
        for (int i = 3; i < 60; ++i)
            pages << QStringLiteral("Running head\nSome body text on this page.\n%1 | For more free content").arg(i - 2);
        QCOMPARE(BookText::pageNumberOffset(pages), 2);
        QCOMPARE(BookText::contentsPages(pages), QList<int>{2});
        // Contents without page numbers, over two pages, from an ebook.
        QStringList ebook{"Cover", "CONTENTS\nCover\nTitle Page\nCHAPTER 1: BASICS\n1.1 One\n1.2 Two\nCHAPTER 2: MORE",
                          "2.1 Three\n2.2 Four\nCHAPTER 3: LAST\n3.1 Five\nA.1 Appendix Part", "Chapter 1\nBasics"};
        QCOMPARE(BookText::contentsPages(ebook), (QList<int>{1, 2}));
        QVERIFY(BookText::sameHeading("Biochemistry", "Chapter 4 Biochemistry"));
        QVERIFY(!BookText::sameHeading("Enzymes", "Chapter 4 Biochemistry"));
        QVERIFY(BookText::sameHeading("Social Structure, Group Ildentity", "Chapter 19 Social Structure, Group Identity"));
        QVERIFY(!BookText::sameHeading("Freestanding Questions: The Circulatory System", "Chapter 14 The Circulatory System"));
        const auto lines = BookText::contentsLines(pages[2], int(pages.size()));
        QCOMPARE(lines.size(), 3);   // "Index 99" is past the last page
        QCOMPARE(lines[0].title, QString("Chapter 1: Rivers"));
        QCOMPARE(lines[1].printedPage, 15);
        // Pages without numbers give no offset rather than a guess.
        QCOMPARE(BookText::pageNumberOffset({"a", "b", "c", "d"}), INT_MIN);
        QVERIFY(BookText::digest(pages, 0, 1).startsWith("[page 1] Title page\n[page 2] Copyright"));
    }

    void bookTextReadsModelAnswers() {
        const QByteArray answer = "```json\n{\"entries\": [{\"title\": \"  Chapter 2 \", \"page\": 12, \"level\": 0},"
                                  "{\"title\": \"Lost\", \"page\": 400, \"level\": 0},"
                                  "{\"title\": \"Deep\", \"page\": 13, \"level\": 9}]}\n```";
        const QVariantList entries = BookText::parseEntries(answer, 100);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].toMap()["label"].toString(), QString("Chapter 2"));
        QCOMPARE(entries[0].toMap()["target"].toInt(), 11);
        QCOMPARE(entries[1].toMap()["depth"].toInt(), 2);
        QVERIFY(BookText::parseEntries("not json", 10).isEmpty());

        const QVariantList earlier{QVariantMap{{"label", "chapter 2"}, {"depth", 0}, {"target", 11}},
                                   QVariantMap{{"label", "Chapter 1"}, {"depth", 0}, {"target", 0}}};
        const QVariantList merged = BookText::mergeEntries(earlier, entries);
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged[0].toMap()["label"].toString(), QString("Chapter 1"));
        QCOMPARE(merged[2].toMap()["label"].toString(), QString("Deep"));
    }

    void bookTextTidiesWhatTheModelFound() {
        QStringList pages;
        for (int i = 0; i < 40; ++i)
            pages << QStringLiteral("MCAT WORKOUT\nBody text on page %1 with words.").arg(i + 1);
        const QStringList heads = BookText::runningHeads(pages);
        QCOMPARE(heads, QStringList{"MCAT WORKOUT"});
        auto entry = [](const QString &label, int page) {
            return QVariant(QVariantMap{{"label", label}, {"depth", 0}, {"target", page}});
        };
        const QVariantList found{
            entry("Part 2: MCAT Biochemistry", 2), entry("Chapter 3: CARS Strategy", 2),
            entry("MCAT WORKOUT", 3),                                     // only a running head
            entry("MCAT WORKOUT FREESTANDING QUESTIONS: ENZYMES AND INHIBITION", 4),
            entry("Freestanding Questions: Enzymes and Inhibition", 5),  // continues the last one
            entry("CHAPTER 20: PERSONALITY, MOTIVATION, AND SELF-IDENTITY", 9),
            entry("Summary", 10), entry("Summary", 30)};                  // far apart: two sections
        const QVariantList tidy = BookText::tidyEntries(found, heads, "Princeton Review MCAT Workout");
        QStringList labels;
        QList<int> depths;
        for (const QVariant &v : tidy) {
            labels << v.toMap()["label"].toString();
            depths << v.toMap()["depth"].toInt();
        }
        QCOMPARE(labels, (QStringList{"Part 2: MCAT Biochemistry", "Chapter 3: CARS Strategy",
                                      "Freestanding Questions: Enzymes and Inhibition",
                                      "Chapter 20: Personality, Motivation, and Self-Identity", "Summary", "Summary"}));
        QCOMPARE(depths, (QList<int>{0, 0, 1, 0, 1, 1}));

        // Parts come from the printed contents, and a heading naming one
        // becomes it.
        const QStringList parts = BookText::contentsParts("TABLE OF CONTENTS\nPART 3: MCAT BIOLOGY\n"
                                                          "CHAPTER 9: GENES ..... 97\nPart 4 - Psychology", "MCAT Workout");
        QCOMPARE(parts, (QStringList{"Part 3: MCAT Biology", "Part 4: Psychology"}));
        const QVariantList withParts = BookText::tidyEntries({entry("MCAT Biology", 96), entry("Chapter 9: Genes", 96)},
                                                             heads, "MCAT Workout", parts);
        QCOMPARE(withParts[0].toMap()["label"].toString(), QString("Part 3: MCAT Biology"));
        const QStringList dnaPages(5, QString("This is the way DNA is copied, and the role of DNA in why it matters."));
        QCOMPARE(BookText::tidyEntries({entry("5.3 THE ROLE OF DNA", 3)}, {}, "Review", {}, dnaPages)[0]
                     .toMap()["label"].toString(),
                 QString("5.3 The Role of DNA"));
        QCOMPARE(withParts[1].toMap()["depth"].toInt(), 0);   // Part 4 was not found, so no nesting

        // Title pages announce chapters; an OCR'd "Chapter 11" before
        // Chapter 2 is Chapter 1.
        const QVariantList titled = BookText::titlePages({"Chapter 15 The Excretory and Digestive Systems",
                                                          "Body text " + QString(200, 'x'), "Chapter1l MCAT Basics",
                                                          "Chapter 4y Enzymes and Inhibition", "CHAPTER 9 GENES"});
        QCOMPARE(titled.size(), 4);
        QCOMPARE(titled[2].toMap()["label"].toString(), QString("Chapter 4 Enzymes and Inhibition"));
        QCOMPARE(titled[0].toMap()["target"].toInt(), 0);
        QCOMPARE(titled[1].toMap()["label"].toString(), QString("Chapter 1 MCAT Basics"));
        const QVariantList renumbered = BookText::tidyEntries({entry("Chapter 11 MCAT Basics", 14),
                                                               entry("Chapter 2 Overview", 22)}, heads, "MCAT Workout");
        QCOMPARE(renumbered[0].toMap()["label"].toString(), QString("Chapter 1: MCAT Basics"));
        const QVariantList furniture = BookText::tidyEntries({entry("Drill MCAT WORKOUT PRACTICE PASSAGE 1", 3)},
                                                             heads, "MCAT Workout");
        QCOMPARE(furniture[0].toMap()["label"].toString(), QString("Practice Passage 1"));
    }

    void bookTextKeepsChaptersInOrderAndDropsInventions() {
        auto entry = [](const QString &label, int page) {
            return QVariant(QVariantMap{{"label", label}, {"depth", 0}, {"target", page}});
        };
        QStringList pages(12, QString("ordinary body text"));
        pages[2] = "4.1 Thermodynamics Free energy and spontaneity of reactions";
        pages[3] = "Enzymes speed up reactions.";
        const QVariantList found{
            entry("Chapter 4: Biochemistry", 1),
            entry("4.1 Thermodynamics", 2),                       // printed on its page: kept
            entry("Chapter 14: Thermodynamics and Enzymes", 2),   // a section a model called a chapter
            entry("Enzyme Specificity and Substrate Recognition", 3), // describes the page; not printed
            entry("thought process needed to get to the answer.", 4),
            entry("4. A mixture containing five amino acids", 5),
            entry("Figure 6 A Beta-Pleated Sheet", 6),
            entry("Chapter 4: Summary", 7),                       // a section of Chapter 4
            entry("Chapter 5: Molecular Biology", 9),
            entry("Chapter 5", 10)};                              // nothing left once it is a section
        pages[7] = "Summary of the chapter";
        const QVariantList tidy = BookText::tidyEntries(found, {}, "Biology Review", {}, pages);
        QStringList labels;
        for (const QVariant &v : tidy)
            labels << v.toMap()["label"].toString();
        QCOMPARE(labels, (QStringList{"Chapter 4: Biochemistry", "4.1 Thermodynamics", "Summary",
                                      "Chapter 5: Molecular Biology"}));
        QCOMPARE(tidy[2].toMap()["depth"].toInt(), 1);

        // Entries copied off a contents page early on do not decide the
        // order: the longest steady run of chapter numbers does.
        QStringList bookPages(40, QString("Uses DNA and ATP in ordinary sentences about cells and DNA and ATP and more DNA."));
        const QVariantList copied = BookText::tidyEntries(
            {entry("Chapter 1: Basics", 2), entry("Chapter 4: Molecular Biology", 3), entry("Chapter 7: Genetics", 4),
             entry("Chapter 2: Strategy", 10), entry("Chapter 3: Molecules", 15), entry("CHAPTER 4: DNA BASICS", 20),
             entry("Chapter 5: Cells", 25), entry("C6H12O6 + 6 O2 = 6 CO2", 26)},
            {}, "Review", {}, bookPages);
        QStringList chapters;
        for (const QVariant &v : copied)
            chapters << v.toMap()["label"].toString();
        QCOMPARE(chapters, (QStringList{"Chapter 1: Basics", "Chapter 2: Strategy", "Chapter 3: Molecules",
                                        "Chapter 4: DNA Basics", "Chapter 5: Cells"}));

        // Chapter 1 listed on its contents page moves to where it starts.
        QStringList early(20, QString("text"));
        early[9] = "MCAT Basics So you want to be a doctor";
        const QVariantList moved = BookText::tidyEntries({entry("Chapter 1: MCAT Basics", 2), entry("MCAT Basics", 9),
                                                          entry("Chapter 2: Strategy", 15)}, {}, "MCAT Review", {}, early);
        QCOMPARE(moved.size(), 2);
        QCOMPARE(moved[0].toMap()["target"].toInt(), 9);
    }

    void bookTextNestsSectionsAndSubsections() {
        auto entry = [](const QString &label, int page, int level = 1) {
            return QVariant(QVariantMap{{"label", label}, {"depth", level}, {"level", level}, {"target", page}});
        };
        const QVariantList tidy = BookText::tidyEntries(
            {entry("Chapter 3: Molecules", 1, 0), entry("3.1 Protein Building Blocks", 2),
             entry("Classification of Amino Acids", 3), entry("3.1.1 Side Chains", 4), entry("3.2 Protein Structure", 5),
             entry("Summary", 8), entry("Chapter 4: Biochemistry", 9, 0), entry("Enzymes", 10, 1),
             entry("Inhibitors", 11, 2)},
            {}, "Review");
        QList<int> depths;
        for (const QVariant &v : tidy)
            depths << v.toMap()["depth"].toInt();
        QCOMPARE(depths, (QList<int>{0, 1, 2, 2, 1, 1, 0, 1, 2}));
        // Headings are listed with their size in the digest.
        const QString d = BookText::digest({"3.1 PROTEIN BUILDING BLOCKS Proteins are made of amino acids."}, 0, 0, 240, {},
                                           {"1.14\t3.1 PROTEIN BUILDING BLOCKS"});
        QVERIFY(d.contains("Headings: \"3.1 PROTEIN BUILDING BLOCKS\" (medium)"));
    }

    void bookTextTakesSectionsFromMeasuredHeadings() {
        QStringList headings(10);
        headings[2] = "1.14\t3.1 PROTEIN BUILDING BLOCKS\n1.14\tAmino Acid Structure and Nomenclature";
        headings[3] = "1.12\tClassification of Amino Acids\n1.09\tD) Isoleucine\n1.2\tDrill";
        for (int i : {4, 5, 6, 7})
            headings[i] = "1.2\tDrill";
        const QSet<QString> repeated = BookText::repeatedHeadings(headings);
        QVERIFY(repeated.contains("drill"));
        QVERIFY(BookText::headingNoise(headings) < 0.09);
        QVERIFY(BookText::headingNoise({"1.2\tP| as [ar |\n1.3\tHn OH\n1.2\tScoring"}) > 0.5);
        // For a noisy scan, only clear headings of the book's own words pass.
        const auto words = BookText::vocabulary(QStringList(5, "practice passage solutions freestanding questions"));
        const QVariantList strict = BookText::headingEntries(
            {"1.3\tPractice Passage 1\n1.3\tBio/Biochem, and i Bie se\n1.3\tP| as [ar |\n1.1\tSolutions\n"
             "1.3\tP Solutions\n1.3\tSOLUTIONS Practice passage questions"}, 0, 0, {}, words);
        QCOMPARE(strict.size(), 1);
        QCOMPARE(strict[0].toMap()["label"].toString(), QString("Practice Passage 1"));
        const QVariantList found = BookText::headingEntries(headings, 0, 9, repeated);
        QStringList labels;
        for (const QVariant &v : found)
            labels << v.toMap()["label"].toString();
        QCOMPARE(labels, (QStringList{"3.1 PROTEIN BUILDING BLOCKS", "Amino Acid Structure and Nomenclature",
                                      "Classification of Amino Acids"}));
        // Print order survives merging with a chapter on the same page.
        const QVariantList merged = BookText::mergeEntries(
            found, {QVariantMap{{"label", "Chapter 3: Molecules"}, {"depth", 0}, {"target", 2}}});
        QCOMPARE(merged[0].toMap()["label"].toString(), QString("Chapter 3: Molecules"));
        QCOMPARE(merged[2].toMap()["label"].toString(), QString("Amino Acid Structure and Nomenclature"));
    }

    void bookTextPrefersChapterTitlePages() {
        auto entry = [](const QString &label, int page, bool titlePage = false) {
            QVariantMap e{{"label", label}, {"depth", 0}, {"target", page}};
            if (titlePage)
                e["titlePage"] = true;
            return QVariant(e);
        };
        // The model put Chapter 19 three pages late, spelled right; the title
        // page has it on time, misread.
        const QVariantList tidy = BookText::tidyEntries(
            BookText::mergeEntries({}, {entry("Chapter 18: Research Methods", 196),
                                        entry("Chapter 19: Social Structure, Group Identity, and Self-Identity", 209),
                                        entry("Chapter 19 Social Structure, Group Ildentity, and Self-ldentity", 206, true),
                                        entry("Chapter 20: Personality ......", 222)}),
            {}, "MCAT Workout");
        QCOMPARE(tidy.size(), 3);
        QCOMPARE(tidy[1].toMap()["target"].toInt(), 206);
        // A section repeating a title-page chapter's name does not move it.
        const QVariantList stays = BookText::tidyEntries(
            {entry("Chapter 19 Social Structure", 206, true), entry("Social Structure", 209)}, {}, "Workout");
        QCOMPARE(stays[0].toMap()["target"].toInt(), 206);
        QCOMPARE(tidy[1].toMap()["label"].toString(), QString("Chapter 19: Social Structure, Group Identity, and Self-Identity"));
        QCOMPARE(tidy[2].toMap()["label"].toString(), QString("Chapter 20: Personality"));
    }

    void outlinesBuildWithAModel() {
        QTemporaryDir dir;
        QStringList text;
        for (int i = 0; i < 70; ++i)
            text << QStringLiteral("This is page %1 of a long scanned book about rivers and mountains.").arg(i + 1);
        const QString pdf = writeBookPdf(dir.filePath("book.pdf"), text);
        const QString id = "feed01";
        int asked = 0;
        QStringList prompts;
        {
            Outlines outlines(nullptr, dir.filePath("outlines"), dir.filePath("text"));
            outlines.setOcr(false);
            outlines.setAsk([&](const QString &, const QString &prompt, const QJsonObject &schema, const auto &done) {
                ++asked;
                prompts << prompt;
                QVERIFY(schema.contains("properties"));
                // The first batch has a chapter on page 1, the second on page 40.
                const QString answer = prompt.contains("[page 1] ")
                    ? "{\"entries\": [{\"title\": \"Chapter One\", \"page\": 1, \"level\": 0}]}"
                    : prompt.contains("[page 40] ") ? "{\"entries\": [{\"title\": \"Chapter Two\", \"page\": 40, \"level\": 0}]}"
                                                    : "{\"entries\": []}";
                QTimer::singleShot(0, [done, answer]() { done(answer, {}); });
            });
            outlines.generate(id, pdf, "Rivers");
            QTRY_COMPARE_WITH_TIMEOUT(outlines.status(id)["state"].toString(), QString("done"), 10000);
            QCOMPARE(asked, 3);   // 70 pages, 30 at a time
            QVERIFY(prompts[0].contains("Book: Rivers"));
            QVERIFY(prompts[0].contains("[page 30] ") && !prompts[0].contains("[page 31] "));
            QCOMPARE(outlines.entries(id).size(), 2);
            QCOMPARE(outlines.entries(id)[1].toMap()["target"].toInt(), 39);
        }
        // Kept for next time, and the page text is cached for another try.
        Outlines again(nullptr, dir.filePath("outlines"), dir.filePath("text"));
        QCOMPARE(again.entries(id).size(), 2);
        QCOMPARE(again.status(id)["state"].toString(), QString("done"));
        QVERIFY(QFile::exists(dir.filePath("text/feed01.json")));
        again.clear(id);
        QVERIFY(again.entries(id).isEmpty());
    }

    void outlinesKeepWhatWasFoundWhenTheModelFails() {
        QTemporaryDir dir;
        QStringList text;
        for (int i = 0; i < 45; ++i)
            text << QStringLiteral("Page %1 holds plenty of ordinary words for the test to read.").arg(i + 1);
        const QString pdf = writeBookPdf(dir.filePath("book.pdf"), text);
        Outlines outlines(nullptr, dir.filePath("outlines"), dir.filePath("text"));
        outlines.setOcr(false);
        int asked = 0;
        outlines.setAsk([&](const QString &, const QString &, const QJsonObject &, const auto &done) {
            const bool first = ++asked == 1;
            QTimer::singleShot(0, [done, first]() {
                first ? done("{\"entries\": [{\"title\": \"Plenty of Ordinary Words\", \"page\": 2, \"level\": 0}]}", {})
                      : done({}, "Ollama is not running.");
            });
        });
        outlines.generate("beef02", pdf, "Words");
        QTRY_COMPARE_WITH_TIMEOUT(outlines.status("beef02")["state"].toString(), QString("failed"), 10000);
        QCOMPARE(outlines.status("beef02")["message"].toString(), QString("Ollama is not running."));
        QCOMPARE(outlines.entries("beef02").size(), 1);
        QVERIFY(outlines.entries("../x").isEmpty());
    }

    void aiPromptsCarryThePassageAndContext() {
        const QVariantMap input{{"text", "Cardiac output is heart rate times stroke volume."},
                                {"context", "A paragraph about trained athletes and their hearts."},
                                {"title", "MCAT Workout"}, {"question", "Why is resting HR low?"}};
        const QString explain = AiRequest::prompt("explain", input);
        QVERIFY(explain.contains("Book: MCAT Workout"));
        QVERIFY(explain.contains("trained athletes"));
        QVERIFY(explain.contains("stroke volume"));
        QVERIFY(explain.contains("Explain"));
        QVERIFY(AiRequest::prompt("quiz", input).contains("**Q:**"));
        QVERIFY(AiRequest::prompt("ask", input).contains("Why is resting HR low?"));
        // A whole chapter selected by accident is cut to fit.
        const QString huge = AiRequest::prompt("summarize", {{"text", QString(20000, 'a')}});
        QVERIFY(huge.size() < 6000);
        // Without Ollama, a request says so rather than hanging.
        AiRequest request;
        request.run("explain", input);
        QVERIFY(!request.running());
        QVERIFY(!request.error().isEmpty());
    }

    void outlinesAreNumbered() {
        QFile file(QStringLiteral(SOURCE_DIR) + "/numbering.js");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString script = QString::fromUtf8(file.readAll());
        script.remove(".pragma library");
        QJSEngine engine;
        engine.evaluate(script);
        QVERIFY(!engine.hasError());
        auto number = [&](const QString &json) {
            const QJSValue out = engine.globalObject().property("labelled").call({engine.toScriptValue(
                QJsonDocument::fromJson(json.toUtf8()).toVariant())});
            QStringList labels;
            for (int i = 0; i < out.property("length").toInt(); ++i)
                labels << out.property(i).property("label").toString();
            return labels;
        };
        // Printed section numbers are kept; unnumbered subsections continue
        // from them; closing matter gets the next number.
        QCOMPARE(number(R"([{"label":"Copyright","depth":0,"target":1},
                            {"label":"Chapter 3: Molecules","depth":0,"target":5},
                            {"label":"3.1 Protein Building Blocks","depth":1,"target":6},
                            {"label":"Amino Acids","depth":2,"target":7},
                            {"label":"Classification","depth":2,"target":8},
                            {"label":"3.2 Protein Structure","depth":1,"target":9},
                            {"label":"The Peptide Bond","depth":2,"target":10},
                            {"label":"Summary","depth":1,"target":11}])"),
                 (QStringList{"Copyright", "Chapter 3: Molecules", "3.1  Protein Building Blocks", "3.1.1  Amino Acids",
                              "3.1.2  Classification", "3.2  Protein Structure", "3.2.1  The Peptide Bond", "3.3  Summary"}));
        // Unnumbered books get numbers, parts stay as they are.
        QCOMPARE(number(R"([{"label":"Part 1: Basics","depth":0,"target":0},
                            {"label":"Rivers","depth":1,"target":1},
                            {"label":"Sources","depth":2,"target":2},
                            {"label":"Mountains","depth":1,"target":3},
                            {"label":"Part 2: More","depth":0,"target":4},
                            {"label":"Deserts","depth":1,"target":5}])"),
                 (QStringList{"Part 1: Basics", "1  Rivers", "1.1  Sources", "2  Mountains", "Part 2: More", "3  Deserts"}));
    }

    void pdfDoubleClickFindsEveryWord() {
        QTemporaryDir dir;
        const QString pdf = writeBookPdf(dir.filePath("words.pdf"),
            {"Proteins are biological macromolecules that act as enzymes and hormones. "
             "Enzymes lower the activation energy of reactions."});
        QPdfDocument doc;
        QCOMPARE(doc.load(pdf), QPdfDocument::Error::None);
        const QString text = doc.getAllText(0).text();
        PdfGeometry geometry;
        const QRegularExpression word("[A-Za-z]+");
        int words = 0;
        for (auto it = word.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            const QPointF middle = doc.getSelectionAtIndex(0, m.capturedStart(), m.capturedLength()).boundingRectangle().center();
            // A double click in the middle of any word, first ones included, selects it.
            QCOMPARE(geometry.select(pdf, 0, middle, middle, "word").value("text").toString(), m.captured());
            ++words;
        }
        QVERIFY(words > 10);
        // A triple click selects the sentence it lands in.
        const int at = int(text.indexOf("activation"));
        const QPointF inSecond = doc.getSelectionAtIndex(0, at, 3).boundingRectangle().center();
        QCOMPARE(geometry.select(pdf, 0, inSecond, inSecond, "sentence").value("text").toString(),
                 QString("Enzymes lower the activation energy of reactions."));
    }

    void textVersionRebuildsParagraphs() {
        using Reflow::Line;
        auto line = [](const QString &text, qreal x, qreal y, qreal w = 400, qreal h = 10) {
            return Line{text, QRectF(x, y, w, h)};
        };
        const QList<Line> lines{
            line("MCAT WORKOUT", 50, 20, 120),                          // running head
            line("3.1 PROTEIN BUILDING BLOCKS", 50, 60, 300, 14),       // a heading, larger
            line("Proteins are large molecules that act as en-", 50, 90),
            line("zymes and support structures in cells.", 50, 102),
            line("They are made of amino acids.", 50, 114),
            line("1. Which amino acid is basic?", 50, 140),
            line("A. Lysine", 60, 154, 100),
            line("B. Glycine", 60, 166, 100),
            line("146 | For more free content", 50, 700, 200)};             // page number and footer
        const QList<Reflow::Block> blocks = Reflow::blocks(lines, 10, 500, {"mcatworkout"});
        QStringList out;
        for (const auto &b : blocks)
            out << QStringLiteral("%1:%2").arg(b.kind).arg(b.text);
        QCOMPARE(out, (QStringList{
            "1:3.1 PROTEIN BUILDING BLOCKS",
            "0:Proteins are large molecules that act as enzymes and support structures in cells. They are made of amino acids.",
            "0:1. Which amino acid is basic?",
            "2:A. Lysine",
            "2:B. Glycine"}));

        // A tall gap between lines is a figure region.
        const QList<QRectF> regions = Reflow::figureRegions(
            {line("Text above the figure.", 50, 100), line("Text below the figure.", 50, 400)}, 10, QSizeF(500, 700));
        QCOMPARE(regions.size(), 1);
        QVERIFY(regions[0].top() > 105 && regions[0].bottom() < 400);
    }

    void zipWriterMakesReadableArchives() {
        ZipWriter zip;
        zip.add("mimetype", "application/epub+zip", false);
        zip.add("OEBPS/a.xhtml", QByteArray(5000, 'a'));
        QTemporaryDir dir;
        const QString path = dir.filePath("t.zip");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(zip.finish());
        file.close();
        ZipReader reader(path);
        QCOMPARE(reader.read("mimetype"), QByteArray("application/epub+zip"));
        QCOMPARE(reader.read("OEBPS/a.xhtml"), QByteArray(5000, 'a'));
    }

    void scansAreCleanedAndSharpened() {
        // A light page: gray paper with a yellow tint and gray text.
        QImage page(400, 300, QImage::Format_RGB32);
        page.fill(QColor(200, 190, 160));
        {
            QPainter p(&page);
            p.setPen(QColor(80, 80, 80));
            QFont font;
            font.setPixelSize(22);
            p.setFont(font);
            for (int y = 40; y < 280; y += 40)
                p.drawText(20, y, "The heart pumps blood through the body.");
        }
        const QImage cleaned = CleanPageProvider::clean(page);
        // Paper turns white (tint gone), text gets darker.
        QVERIFY(qGray(cleaned.pixel(395, 295)) > 245);
        QVERIFY(std::abs(qRed(cleaned.pixel(395, 295)) - qBlue(cleaned.pixel(395, 295))) < 8);

        // Blurring lowers sharpness; sharpening brings it back up.
        const QImage blurry = page.scaled(100, 75, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                  .scaled(400, 300, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QVERIFY(CleanPageProvider::sharpness(blurry) < CleanPageProvider::sharpness(page));
        QVERIFY(CleanPageProvider::sharpness(CleanPageProvider::sharpen(blurry)) > CleanPageProvider::sharpness(blurry) * 1.5);

        // A dark page with light text keeps its polarity: background darker,
        // text brighter.
        QImage dark(400, 200, QImage::Format_RGB32);
        dark.fill(QColor(70, 70, 70));
        {
            QPainter p(&dark);
            p.fillRect(QRect(150, 80, 120, 40), QColor(170, 170, 170));
        }
        // A page of lines tilted by 1.5 degrees is found tilted, and comes
        // out level; a dark scanner edge is painted out.
        QImage tilted(800, 1000, QImage::Format_RGB32);
        tilted.fill(QColor(235, 232, 225));
        {
            QPainter p(&tilted);
            p.translate(400, 500);
            p.rotate(1.5);
            p.translate(-400, -500);
            for (int y = 100; y < 900; y += 30)
                p.fillRect(QRect(100, y, 600, 8), QColor(40, 40, 40));
            p.resetTransform();
            p.fillRect(QRect(0, 0, 30, 1000), QColor(20, 20, 20));
        }
        const double tilt = CleanPageProvider::skewAngle(tilted);
        QVERIFY2(std::abs(std::abs(tilt) - 1.5) < 0.2, qPrintable(QString::number(tilt)));
        QVERIFY(std::abs(CleanPageProvider::skewAngle(CleanPageProvider::deskew(tilted))) < 0.3);
        QVERIFY(qGray(CleanPageProvider::clearBorders(tilted).pixel(10, 500)) > 200);

        // A shadow fading in from the left edge is evened out.
        QImage shaded(600, 400, QImage::Format_RGB32);
        for (int x = 0; x < 600; ++x) {
            const int v = x < 150 ? 120 + x * 100 / 150 : 220;
            for (int y = 0; y < 400; ++y)
                shaded.setPixel(x, y, qRgb(v, v, v - 10));
        }
        {
            QPainter p(&shaded);
            for (int y = 40; y < 380; y += 30)
                p.fillRect(QRect(40, y, 520, 6), QColor(30, 30, 30));
        }
        const QImage even = CleanPageProvider::flattenBackground(shaded);
        QVERIFY(std::abs(qGray(even.pixel(20, 25)) - qGray(even.pixel(400, 25))) < 35);   // was ~100 apart
        QVERIFY(qGray(even.pixel(300, 42)) < 80);   // text stays dark
        // A solid black box is content, not shadow: it stays black.
        QImage boxed = shaded;
        {
            QPainter p(&boxed);
            p.fillRect(QRect(300, 100, 200, 120), QColor(20, 20, 20));
        }
        QVERIFY(qGray(CleanPageProvider::flattenBackground(boxed).pixel(400, 160)) < 60);

        // A colourful cover is left as it is.
        QImage cover(300, 400, QImage::Format_RGB32);
        cover.fill(QColor(20, 60, 160));
        {
            QPainter p(&cover);
            p.fillRect(QRect(40, 100, 220, 60), QColor(240, 200, 30));
        }
        QCOMPARE(CleanPageProvider::clean(cover).pixel(10, 10), cover.pixel(10, 10));

        const QImage lifted = CleanPageProvider::clean(dark);
        QVERIFY(qGray(lifted.pixel(10, 10)) < 40);
        QVERIFY(qGray(lifted.pixel(200, 100)) > 230);
    }

    void pdfSelectionMergesIntoLines() {
        auto glyph = [](qreal x, qreal y) { return QPolygonF(QRectF(x, y, 6, 10)); };
        // Two lines of a left column, then a word across the column gap.
        const QList<QPolygonF> glyphs{glyph(10, 10), glyph(16, 10), glyph(23, 10.5), glyph(10, 24), glyph(16, 24),
                                      glyph(300, 10), glyph(306, 10)};
        const QVariantList lines = PdfGeometry().lineRects(glyphs);
        QCOMPARE(lines.size(), 3);
        const QVariantList first = lines.first().toList();
        QCOMPARE(first.size(), 8);
        QCOMPARE(first[0].toDouble(), 10.0);
        QCOMPARE(first[2].toDouble(), 29.0);
        QCOMPARE(first[5].toDouble(), 20.5);
        QCOMPARE(lines[2].toList()[0].toDouble(), 300.0);
        QVERIFY(PdfGeometry().lineRects({}).isEmpty());
    }

    void annotationsRefuseOddBookIds() {
        QTemporaryDir dir;
        Annotations a(dir.path());
        QVERIFY(a.add("../escape", {{"text", "x"}}).isEmpty());
        QVERIFY(a.list("../escape").isEmpty());
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 0);
    }

    void annotationsExportMarkdown() {
        QTemporaryDir dir;
        Annotations a(dir.filePath("store"));
        a.add("b1", {{"kind", "highlight"}, {"position", 0.2}, {"text", "Line one\nLine two"},
                     {"note", "My thought"}, {"chapter", "Chapter 1"}});
        a.add("b1", {{"kind", "bookmark"}, {"position", 0.5}, {"chapter", "Chapter 2"}});
        const QString md = a.markdown("b1", "A Book", "Someone");
        QVERIFY(md.startsWith("# A Book\n"));
        QVERIFY(md.contains("## Chapter 1"));
        QVERIFY(md.contains("> Line one\n> Line two\n"));
        QVERIFY(md.contains("My thought"));
        QVERIFY(md.indexOf("Chapter 1") < md.indexOf("Chapter 2"));

        const QString path = a.exportMarkdown("b1", "A/Book: Notes?", "Someone", dir.filePath("out"));
        QVERIFY2(!path.isEmpty(), qPrintable(a.lastError()));
        QVERIFY(!QFileInfo(path).fileName().contains('/'));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), a.markdown("b1", "A/Book: Notes?", "Someone"));
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", "{\"Save\": \"Guardar\", \"Open\": \"Abrir\"}");
        write(dir.path() + "/pt-BR.json", "{\"Save\": \"Salvar\"}");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    // A reference manual numbered by chapter ("2-3"): chapters from the
    // labels and running heads, every section the chapter lists (found in
    // the text or not), and named equation blocks as subsections.
    void printedOutlineReadsChapterNumberedBooks() {
        const QStringList titles{"Light and Sound", "Heat and Energy", "Fluid Motion", "Wave Mechanics"};
        const QString filler = QStringLiteral(
            "The alpha waves and beta particles carry gamma ray energy through the sample. Light and sound, "
            "heat and energy, fluid motion, and wave mechanics are reviewed in this manual with examples.\n");
        QStringList pages;
        for (int c = 1; c <= 4; ++c) {
            for (int p = 1; p <= 6; ++p) {
                QString page;
                if (p == 1) {
                    page = titles[c - 1] + "\n"
                         + QStringLiteral("1. Alpha Waves .............. %1-1\n2. Beta Particles ........ %1-3\n"
                                          "3. Gamma Rays ............ %1-5\n4. Delta Energy ........ ...\n").arg(c)
                         + "1. ALPHA WAVES\n";
                } else if (p % 2 == 0) {
                    page = QStringLiteral("%1-%2 SAMPLE REVIEW MANUAL\n").arg(c).arg(p);
                } else {
                    page = QStringLiteral("%1 %2-%3\n").arg(titles[c - 1].toUpper()).arg(c).arg(p);
                }
                if (p == 3)
                    page += "2. BETA PARTICLES\n";
                if (p == 5)
                    page += "3. GAMMA RAYS\nEquation 3.4: Gamma Ray Energy\n";
                page += filler + filler;
                pages << page;
            }
        }
        const QVariantList outline = BookText::printedOutline(pages);
        QStringList chapters;
        for (const QVariant &v : outline)
            if (v.toMap()["depth"].toInt() == 0)
                chapters << v.toMap()["label"].toString();
        QCOMPARE(chapters, QStringList({"Chapter 1: Light and Sound", "Chapter 2: Heat and Energy",
                                        "Chapter 3: Fluid Motion", "Chapter 4: Wave Mechanics"}));
        auto find = [&](const QString &label) {
            for (const QVariant &v : outline)
                if (v.toMap()["label"].toString() == label)
                    return v.toMap();
            return QVariantMap();
        };
        QCOMPARE(find("2.1 Alpha Waves")["target"].toInt(), 6);
        QCOMPARE(find("2.2 Beta Particles")["target"].toInt(), 8);
        QCOMPARE(find("2.3 Gamma Rays")["target"].toInt(), 10);
        QCOMPARE(find("2.3 Gamma Rays")["depth"].toInt(), 1);
        // Listed but never printed as a heading: still there, after 2.3.
        QVERIFY(!find("2.4 Delta Energy").isEmpty());
        QCOMPARE(find("Gamma Ray Energy")["depth"].toInt(), 2);
        // Sections come in order within each chapter.
        int last = -1;
        for (const QVariant &v : outline) {
            const int target = v.toMap()["target"].toInt();
            QVERIFY(target >= last);
            last = target;
        }
        // An ordinary book is left alone.
        QVERIFY(BookText::printedOutline(QStringList(30, filler)).isEmpty());
    }

    // Export follows the outline: chapter headings, then section headings.
    void markdownGroupsNotesByOutline() {
        QTemporaryDir dir;
        Annotations notes(dir.path());
        const QString a = notes.add("ab12", {{"kind", "highlight"}, {"location", "3"}, {"position", 0.1}, {"text", "First"}});
        const QString b = notes.add("ab12", {{"kind", "highlight"}, {"location", "4"}, {"position", 0.2}, {"text", "Second"}});
        const QVariantMap places{{a, QVariantMap{{"group", "Chapter 1: Cells"}, {"subgroup", "1.1  Membranes"}}},
                                 {b, QVariantMap{{"group", "Chapter 1: Cells"}, {"subgroup", "1.2  Organelles"}}}};
        const QString md = notes.markdown("ab12", "Biology", "", places);
        QVERIFY2(md.count("## Chapter 1: Cells") == 1, qPrintable(md));
        QVERIFY(md.indexOf("### 1.1  Membranes") < md.indexOf("> First"));
        QVERIFY(md.indexOf("> First") < md.indexOf("### 1.2  Organelles"));
        QVERIFY(md.indexOf("### 1.2  Organelles") < md.indexOf("> Second"));
    }

    // Search finds every word, best matches first, with the words bolded.
    void searchFindsAllWordsAndRanksPhrases() {
        QList<BookSearch::Unit> units{
            {0, "p. 1", "Enzymes lower activation energy. Nothing else here."},
            {1, "p. 2", "The activation energy of a reaction sets its rate."},
            {2, "p. 3", "Energy is conserved; activation is a separate idea."},
            {3, "p. 4", "Only energy here."}};
        const QVariantList hits = BookSearch::find(units, "activation energy", 10);
        QCOMPARE(hits.size(), 3);
        QCOMPARE(hits[0].toMap()["target"].toInt(), 0);   // phrase, and earliest
        QCOMPARE(hits[2].toMap()["target"].toInt(), 2);   // words apart rank last
        QVERIFY(hits[0].toMap()["snippet"].toString().contains("<b>activation</b>", Qt::CaseInsensitive));
        QVERIFY(BookSearch::find(units, "x", 10).isEmpty());
    }

    // Ask this book: the passages with the question's rarer words win, and
    // filler words in the question count for nothing.
    void bestPassagesAnswerTheQuestion() {
        QList<BookSearch::Unit> units;
        for (int i = 0; i < 20; ++i)
            units.append({i, QString("p. %1").arg(i + 1), "The cell is the unit of life and the cell has parts."});
        units.append({20, "p. 21", "Mitochondria make ATP through oxidative phosphorylation in the cell."});
        units.append({21, "p. 22", "Ribosomes build proteins."});
        const QVariantList found = BookSearch::best(units, "How do mitochondria make ATP?", 3);
        QVERIFY(!found.isEmpty());
        QCOMPARE(found[0].toMap()["target"].toInt(), 20);
        QCOMPARE(found[0].toMap()["n"].toInt(), 1);
        QVERIFY(BookSearch::best(units, "what is the", 3).isEmpty());
    }

    void epubTextIsReadBySection() {
        QTemporaryDir dir;
        const QString path = writeEpub(dir.filePath("b.epub"), "Book", "Author");
        const QList<BookSearch::Unit> units = BookSearch::epubUnits(path);
        QVERIFY(!units.isEmpty());
        for (const auto &u : units) {
            QVERIFY(!u.text.contains('<'));
            QVERIFY(u.target >= 0);
        }
    }

    // Model output is checked: a multiple-choice answer must be one of its
    // choices (by text or letter); free answers keep their model answer.
    void practiceQuestionsAreValidated() {
        const QVariantList q = PracticeTest::parseQuestions(R"({"questions":[
            {"type":"choice","question":"Where is ATP made?","choices":["Nucleus","Mitochondria","Ribosome","Golgi"],"answer":"Mitochondria","why":"It says so."},
            {"type":"choice","question":"Letter answer?","choices":["One","Two","Three"],"answer":"C","why":""},
            {"type":"choice","question":"Bad answer","choices":["A1","B1"],"answer":"Neither","why":""},
            {"type":"free","question":"Explain glycolysis.","answer":"Glucose is split into pyruvate.","why":"x"},
            {"type":"free","question":"","answer":"no question"}]})");
        QCOMPARE(q.size(), 3);
        QCOMPARE(q[0].toMap()["answer"].toInt(), 1);
        QCOMPARE(q[1].toMap()["answer"].toInt(), 2);
        QCOMPARE(q[2].toMap()["type"].toString(), QString("free"));
    }

    // "idk" is not sent to the model, and the model's praise is kept only
    // when the student actually wrote what it praises.
    void freeAnswersAreMarkedHonestly() {
        QVERIFY(PracticeTest::isNonAnswer("idk"));
        QVERIFY(PracticeTest::isNonAnswer("I don't know?"));
        QVERIFY(PracticeTest::isNonAnswer("  "));
        QVERIFY(!PracticeTest::isNonAnswer("in extracellular proteins"));
        QCOMPARE(PracticeTest::groundedCredit("disulfide bridges form under oxidizing conditions", "idk"), QString());
        QCOMPARE(PracticeTest::groundedCredit("classification relates to their properties", "To detect proteins"),
                 QString());
        QCOMPARE(PracticeTest::groundedCredit("outside the cell, oxidized", "Outside the cell where it's oxidizing"),
                 QString("outside the cell, oxidized"));
    }

    // Hands-on questions: code needs a solution and tests; a Markov chain's
    // rows must be probabilities, and its answer is computed, not trusted.
    void handsOnQuestionsAreValidated() {
        const QVariantList q = PracticeTest::parseQuestions(R"json({"questions":[
            {"type":"code","question":"Write add.","language":"python","starter":"def add(a, b):\n    pass",
             "solution":"def add(a, b):\n    return a + b","tests":[{"call":"add(1, 2)","expected":"3"},{"call":"","expected":"1"}],
             "answer":"Return a + b.","why":"x"},
            {"type":"code","question":"No tests.","solution":"x = 1","tests":[],"answer":"a","why":""},
            {"type":"markov","question":"Weather.","task":"steady","states":["Sunny","Rainy"],
             "matrix":[[0.9,0.1],[0.5,0.5]],"answer":"Sunny 5/6","why":""},
            {"type":"markov","question":"Bad rows.","task":"build","states":["A","B"],"matrix":[[0.5,0.2],[0.5,0.5]],
             "answer":"a","why":""},
            {"type":"markov","question":"Steps.","task":"steps","states":["A","B"],"matrix":[[0,1],[1,0]],
             "start":"B","steps":3,"answer":"a","why":""},
            {"type":"markov","question":"Two separate states.","task":"steady","states":["A","B"],
             "matrix":[[1,0],[0,1]],"answer":"a","why":""}]})json");
        QCOMPARE(q.size(), 3);
        const QVariantMap code = q[0].toMap();
        QCOMPARE(code["type"].toString(), QString("code"));
        QCOMPARE(code["tests"].toList().size(), 1);
        const QVariantList pi = q[1].toMap()["expected"].toList();
        QCOMPARE(pi.size(), 2);
        QVERIFY(std::abs(pi[0].toDouble() - 5.0 / 6.0) < 1e-6);
        // From B, three steps of a chain that always switches ends in A.
        const QVariantList after = q[2].toMap()["expected"].toList();
        QCOMPARE(q[2].toMap()["start"].toInt(), 1);
        QVERIFY(std::abs(after[0].toDouble() - 1.0) < 1e-9);
        // The periodic chain still has a steady state; two closed states don't.
        QVERIFY(!PracticeTest::steadyState({{0, 1}, {1, 0}}).isEmpty());
        QVERIFY(PracticeTest::steadyState({{1, 0}, {0, 1}}).isEmpty());
    }

    // Molecules must parse and match their formula; scenarios are worked out
    // by Shelf and dropped when the model's own answer disagrees.
    void moleculeAndScenarioQuestionsAreChecked() {
        const QVariantList q = PracticeTest::parseQuestions(R"json({"questions":[
            {"type":"molecule","question":"Draw ethanol.","smiles":"CCO","formula":"C2H5OH","answer":"Ethanol","why":""},
            {"type":"molecule","question":"Wrong formula.","smiles":"CCO","formula":"C3H8","answer":"x","why":""},
            {"type":"molecule","question":"Broken.","smiles":"C1CC","formula":"","answer":"x","why":""},
            {"type":"molecule","question":"Benzene.","smiles":"c1ccccc1","formula":"C6H6","answer":"Benzene","why":""},
            {"type":"scenario","question":"Call payoff?","task":"predict","output":"payoff","vary":"","goal":"equal","target":0,"result":5,
             "variables":[{"name":"S","label":"Stock price","unit":"USD","min":50,"max":150,"value":110},
                          {"name":"K","label":"Strike","unit":"USD","min":50,"max":150,"value":100}],
             "outputs":[{"name":"payoff","label":"Payoff","unit":"USD","formula":"max(S-K,0)-5"}],"answer":"5","why":""},
            {"type":"scenario","question":"Break even?","task":"find","output":"payoff","vary":"S","goal":"equal","target":0,"result":105,
             "variables":[{"name":"S","label":"Stock price","unit":"USD","min":50,"max":150,"value":110},
                          {"name":"K","label":"Strike","unit":"USD","min":50,"max":150,"value":100}],
             "outputs":[{"name":"payoff","label":"Payoff","unit":"USD","formula":"max(S-K,0)-5"}],"answer":"105","why":""},
            {"type":"scenario","question":"Disagrees.","task":"predict","output":"y","vary":"","goal":"equal","target":0,"result":99,
             "variables":[{"name":"x","label":"x","unit":"","min":0,"max":10,"value":2}],
             "outputs":[{"name":"y","label":"y","unit":"","formula":"x^2"}],"answer":"","why":""},
            {"type":"scenario","question":"Undefined name.","task":"predict","output":"y","vary":"","goal":"equal","target":0,"result":1,
             "variables":[{"name":"x","label":"x","unit":"","min":0,"max":10,"value":2}],
             "outputs":[{"name":"y","label":"y","unit":"","formula":"x+w"}],"answer":"","why":""}]})json");
        QCOMPARE(q.size(), 4);
        QCOMPARE(q[0].toMap()["formula"].toString(), QString("C2H6O"));
        QCOMPARE(q[1].toMap()["formula"].toString(), QString("C6H6"));
        QVERIFY(std::abs(q[2].toMap()["expected"].toDouble() - 5) < 1e-9);
        QVERIFY(std::abs(q[3].toMap()["expected"].toDouble() - 105) < 1e-3);
        QCOMPARE(q[3].toMap()["answer"].toString(), QString("Stock price = 105 USD"));
    }

    // The graph joins passages to their chapters and books, and passages in
    // different books to the [[concepts]] they share.
    void knowledgeGraphConnectsBooks() {
        QTemporaryDir dir;
        Annotations a(dir.path());
        a.add("aa11", {{"kind", "highlight"}, {"text", "Glycolysis splits glucose."}, {"chapter", "Metabolism"},
                       {"note", "see [[Glycolysis]] and [[ATP|energy]]"}, {"location", "3"}});
        a.add("bb22", {{"kind", "highlight"}, {"text", "Muscles burn glucose."}, {"chapter", "Exercise"},
                       {"note", "[[glycolysis]] again"}, {"location", "epubcfi(/6/4)"}});
        a.add("bb22", {{"kind", "bookmark"}, {"location", "epubcfi(/6/8)"}});
        KnowledgeGraph graph(&a, nullptr, dir.filePath("concepts.json"));
        const QVariantMap g = graph.graph({QVariantMap{{"id", "aa11"}, {"title", "Biology"}, {"format", "pdf"}},
                                           QVariantMap{{"id", "bb22"}, {"title", "Training"}, {"format", "epub"}}});
        QHash<QString, QVariantMap> nodes;
        for (const QVariant &n : g["nodes"].toList())
            nodes[n.toMap()["id"].toString()] = n.toMap();
        QVERIFY(nodes.contains("b:aa11") && nodes.contains("c:bb22:Exercise"));
        const QVariantMap glycolysis = nodes.value("k:glycolysis");
        QCOMPARE(glycolysis["uses"].toInt(), 2);
        QCOMPARE(glycolysis["books"].toInt(), 2);
        QVERIFY(nodes.contains("k:atp"));
        // Bookmarks are not part of it: 2 books, 2 chapters, 2 passages, 2 concepts.
        QCOMPARE(nodes.size(), 8);
        QCOMPARE(KnowledgeGraph::wikiLinks("a [[B c]] and [[d|e]] [[b c]]"), (QStringList{"B c", "d"}));
        QCOMPARE(graph.pending({QVariantMap{{"id", "aa11"}}}), 1);
    }

    // The voice: each computer gets its own Piper build plus the voice, all
    // with checksums; without a voice, a sentence comes back with no sound
    // at once, so the lesson goes on at reading pace.
    void voiceDownloadsAreChecked() {
        for (const QString platform : {"linux-x86_64", "linux-aarch64", "windows-x86_64"}) {
            const QList<Voice::Download> all = Voice::downloadsFor("en_US-lessac-medium", platform);
            QCOMPARE(all.size(), 3);
            for (const Voice::Download &d : all) {
                QVERIFY(d.url.startsWith("https://"));
                QCOMPARE(d.sha256.size(), 64);
            }
        }
        QVERIFY(Voice::downloadsFor("en_US-lessac-medium", "windows-x86_64")[0].file.endsWith(".zip"));
        // Kokoro: sherpa-onnx for the computer, and the voices, all checked.
        for (const QString platform : {"linux-x86_64", "linux-aarch64", "windows-x86_64"}) {
            const QList<Voice::Download> all = Voice::downloadsFor("kokoro-af_heart", platform);
            QCOMPARE(all.size(), 2);
            QCOMPARE(all[1].file, QString("kokoro/kokoro.tar.bz2"));
            for (const Voice::Download &d : all)
                QCOMPARE(d.sha256.size(), 64);
        }
        QVERIFY(Voice::downloadsFor("nobody/1", "linux-x86_64").isEmpty());
        for (const QString model : {"kokoro-hd", "supertonic", "kitten", "libritts", "lessac-hd", "ryan-hd", "ljspeech-hd"})
            QCOMPARE(Voice::downloadsFor(model + "/0", "windows-x86_64").size(), 2);
        // Voice ids from the first Kokoro version carry over.
        QCOMPARE(Voice::migrate("kokoro-bf_emma"), QString("kokoro/21"));
        QCOMPARE(Voice::migrate("en_US-amy-medium"), QString("en_US-amy-medium"));
        // Each kind of model gets its own flags, and the speaker.
        Voice flags(nullptr, QTemporaryDir().path());
        const QStringList kitten = flags.arguments("kitten/1", "Hello.", "/tmp/x.wav");
        QVERIFY(kitten.join(' ').contains("--kitten-model="));
        QVERIFY(kitten.contains("--sid=1"));
        QCOMPARE(kitten.last(), QString("Hello."));
        QVERIFY(flags.arguments("supertonic/4", "Hi.", "o.wav").contains("--lang=en"));
        QVERIFY(flags.arguments("kokoro-hd/21", "Hi.", "o.wav").join(' ').contains("lexicon-gb-en.txt"));
        QVERIFY(flags.arguments("libritts/900", "Hi.", "o.wav").contains("--sid=900"));
        QVERIFY(Voice::downloadsFor("nobody", "linux-x86_64").isEmpty());
        QVERIFY(Voice::downloadsFor("en_US-amy-medium", "macos").isEmpty());
        QTemporaryDir dir;
        Voice voice(nullptr, dir.path());
        QVERIFY(!voice.ready());
        QSignalSpy spoken(&voice, &Voice::spoken);
        voice.say("lesson", 1, 2, "Hello there.", true);
        QCOMPARE(spoken.size(), 1);
        QCOMPARE(spoken[0][3].toString(), QString());
        voice.say("lesson", 1, 3, "Ahead of time.", false);   // nothing waits for it
        QCOMPARE(spoken.size(), 1);
    }

    // Studio voices are only offered, never assumed: nothing is installed
    // until asked, and speaking without one says so at once.
    void studioVoicesNeedInstalling() {
        QTemporaryDir dir;
        StudioVoice studio(dir.path());
        QVERIFY(!studio.installed("chatterbox"));
        QVERIFY(!studio.installed("xtts"));
        QVERIFY(StudioVoice::isStudio("xtts"));
        QVERIFY(!StudioVoice::isStudio("kokoro"));
        QCOMPARE(studio.xttsSpeakers().size(), 58);
        const QByteArray helper = StudioVoice::helperScript();
        QVERIFY(helper.contains("ChatterboxTTS") && helper.contains("xtts_v2"));
        QString error;
        studio.speak("xtts", "Ana Florence", "Hello.", dir.filePath("x.wav"), [&](const QString &e) { error = e; });
        QVERIFY(!error.isEmpty());
        Voice voice(nullptr, dir.path());
        QVERIFY(!voice.installed("chatterbox/default"));
    }

    // Lessons: good scenes are kept, a formula that can't be typeset or a
    // graph that can't be worked out becomes points, labels get quoted, and
    // narration is cut into sentences.
    void lessonsAreChecked() {
        QJsonArray scenes{
            QJsonObject{{"kind", "formula"}, {"heading", "Falling"}, {"say", "Distance grows with the square of time."},
                        {"latex", "d = \\frac{1}{2} g t^2"},
                        {"terms", QJsonArray{QJsonObject{{"symbol", "g"}, {"meaning", "gravity"}}}}},
            QJsonObject{{"kind", "formula"}, {"heading", "Broken"}, {"say", "This one is broken. It has a bad brace."},
                        {"latex", "\\frac{1}{"}, {"terms", QJsonArray{}}},
            QJsonObject{{"kind", "graph"}, {"heading", "Curve"}, {"say", "Watch it curve."}, {"formula", "0.5*9.81*t^2"},
                        {"variable", "t"}, {"from", 0}, {"to", 3}, {"xLabel", "t"}, {"yLabel", "d"}},
            QJsonObject{{"kind", "graph"}, {"heading", "Unknown"}, {"say", "Uses a name it never defines."},
                        {"formula", "k*t"}, {"variable", "t"}, {"from", 0}, {"to", 3}, {"xLabel", ""}, {"yLabel", ""}},
            QJsonObject{{"kind", "diagram"}, {"heading", "Flow"}, {"say", "First this, then that."},
                        {"mermaid", "flowchart LR\n  A[Glucose (C6)] --> B[Pyruvate]"}},
            QJsonObject{{"kind", "steps"}, {"heading", ""}, {"say", ""}, {"steps", QJsonArray{"x"}}},
        };
        const QVariantMap lesson = Explainer::parseLesson(QJsonDocument(QJsonObject{{"title", "Gravity"}, {"scenes", scenes}}).toJson());
        const QVariantList out = lesson.value("scenes").toList();
        QCOMPARE(out.size(), 5);   // the scene with nothing to say is dropped
        QCOMPARE(out[0].toMap().value("kind").toString(), QString("formula"));
        QCOMPARE(out[0].toMap().value("terms").toList().size(), 1);
        QCOMPARE(out[1].toMap().value("kind").toString(), QString("points"));
        QCOMPARE(out[1].toMap().value("points").toStringList(), (QStringList{"This one is broken.", "It has a bad brace."}));
        QCOMPARE(out[2].toMap().value("kind").toString(), QString("graph"));
        QCOMPARE(out[3].toMap().value("kind").toString(), QString("points"));
        QCOMPARE(out[4].toMap().value("mermaid").toString(), QString("flowchart LR\n  A[\"Glucose (C6)\"] --> B[\"Pyruvate\"]"));
        QCOMPARE(Explainer::sentences("Mass is 2.5 kg, e.g. a bag. Then it falls! Why? Gravity."),
                 (QStringList{"Mass is 2.5 kg, e.g. a bag.", "Then it falls!", "Why?", "Gravity."}));
        QVERIFY(Explainer::parseLesson("{}").isEmpty());
        // An exam review's topics, each with words to find its slides by.
        const QVariantList topics = Explainer::parseTopics(
            R"({"topics": [{"topic": "CUDA memory model", "details": "Explain global vs shared.", "keywords": ["Shared Memory", "global memory"]},
                           {"topic": " ", "details": "", "keywords": []}]})");
        QCOMPARE(topics.size(), 1);
        QCOMPARE(topics[0].toMap().value("keywords").toStringList(), (QStringList{"shared memory", "global memory"}));
    }

    // PowerPoint: slides in presentation order, titles, indented bullets,
    // pictures written out, and speaker notes.
    void pptxSlidesAreRead() {
        QTemporaryDir dir;
        const QByteArray ns = "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                              "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                              "xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";
        const auto shape = [](const QByteArray &ph, const QList<QPair<int, QByteArray>> &paras) {
            QByteArray out = "<p:sp><p:nvSpPr><p:nvPr>" + (ph.isEmpty() ? QByteArray() : "<p:ph type=\"" + ph + "\"/>")
                             + "</p:nvPr></p:nvSpPr><p:txBody>";
            for (const auto &[level, text] : paras)
                out += "<a:p><a:pPr lvl=\"" + QByteArray::number(level) + "\"/><a:r><a:t>" + text + "</a:t></a:r></a:p>";
            return out + "</p:txBody></p:sp>";
        };
        const QByteArray rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
        const QByteArray presentation = "<p:presentation " + ns + "><p:sldIdLst><p:sldId id=\"257\" r:id=\"rId3\"/>"
                                        "<p:sldId id=\"256\" r:id=\"rId2\"/></p:sldIdLst></p:presentation>";
        const QByteArray presentationRels =
            "<Relationships><Relationship Id=\"rId2\" Type=\"" + rel + "slide\" Target=\"slides/slide1.xml\"/>"
            "<Relationship Id=\"rId3\" Type=\"" + rel + "slide\" Target=\"slides/slide2.xml\"/></Relationships>";
        const QByteArray slide1 = "<p:sld " + ns + "><p:cSld><p:spTree>" + shape("title", {{0, "Glycolysis"}})
                                  + shape("body", {{0, "Splits glucose"}, {1, "Makes 2 ATP"}}) + shape("sldNum", {{0, "1"}})
                                  + "<p:pic><p:blipFill><a:blip r:embed=\"rId5\"/></p:blipFill></p:pic>"
                                  + "</p:spTree></p:cSld></p:sld>";
        const QByteArray slide1Rels =
            "<Relationships><Relationship Id=\"rId5\" Type=\"" + rel + "image\" Target=\"../media/image1.png\"/>"
            "<Relationship Id=\"rId6\" Type=\"" + rel + "notesSlide\" Target=\"../notesSlides/notesSlide1.xml\"/></Relationships>";
        const QByteArray notes1 = "<p:notes " + ns + "><p:cSld><p:spTree>" + shape("body", {{0, "Mention the investment phase."}})
                                  + shape("sldNum", {{0, "1"}}) + "</p:spTree></p:cSld></p:notes>";
        const QByteArray slide2 = "<p:sld " + ns + "><p:cSld><p:spTree>" + shape("", {{0, "Overview"}, {0, "Three stages"}})
                                  + "</p:spTree></p:cSld></p:sld>";
        const QString path = dir.filePath("deck.pptx");
        write(path, makeZip({{"ppt/presentation.xml", presentation},
                             {"ppt/_rels/presentation.xml.rels", presentationRels},
                             {"ppt/slides/slide1.xml", slide1},
                             {"ppt/slides/_rels/slide1.xml.rels", slide1Rels},
                             {"ppt/slides/slide2.xml", slide2},
                             {"ppt/notesSlides/notesSlide1.xml", notes1},
                             {"ppt/media/image1.png", pngBytes(Qt::blue)}}, true));
        const QList<Slides::Slide> slides = Slides::readPptx(path, dir.filePath("media"));
        QCOMPARE(slides.size(), 2);
        // Presentation order, not file order: slide2.xml comes first.
        QCOMPARE(slides[0].title, QString("Overview"));
        QCOMPARE(slides[0].paragraphs.size(), 1);
        QCOMPARE(slides[1].title, QString("Glycolysis"));
        QCOMPARE(slides[1].paragraphs.size(), 2);
        QCOMPARE(slides[1].paragraphs[1].level, 1);
        QCOMPARE(slides[1].images.size(), 1);
        QVERIFY(QFileInfo::exists(slides[1].images[0]));
        QCOMPARE(slides[1].notes, QString("Mention the investment phase."));
        QCOMPARE(slides[1].text(), QString("Glycolysis\nSplits glucose\n  Makes 2 ATP"));
        QCOMPARE(Slides::pptxNotes(path), (QStringList{"", "Mention the investment phase."}));
    }

    // An EPUB's sections come from its table of contents, nested, with
    // links made whole; a workspace keeps its decks and links.
    void workspacesKeepSlidesAndLinks() {
        QTemporaryDir dir;
        const QByteArray container = "<?xml version=\"1.0\"?><container xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
                                     "<rootfiles><rootfile full-path=\"OEBPS/content.opf\"/></rootfiles></container>";
        const QByteArray opf = "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\"><manifest>"
                               "<item id=\"nav\" href=\"nav.xhtml\" properties=\"nav\" media-type=\"application/xhtml+xml\"/>"
                               "<item id=\"c1\" href=\"text/c1.xhtml\" media-type=\"application/xhtml+xml\"/>"
                               "</manifest><spine><itemref idref=\"c1\"/></spine></package>";
        const QByteArray nav = "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\"><body>"
                               "<nav epub:type=\"toc\"><ol><li><a href=\"text/c1.xhtml\">Chapter 1 Energy</a><ol>"
                               "<li><a href=\"text/c1.xhtml#s1\">Glycolysis</a></li></ol></li></ol></nav>"
                               "<nav epub:type=\"landmarks\"><ol><li><a href=\"text/c1.xhtml\">Start</a></li></ol></nav></body></html>";
        const QString epub = dir.filePath("book.epub");
        write(epub, makeZip({{"mimetype", "application/epub+zip"}, {"META-INF/container.xml", container},
                             {"OEBPS/content.opf", opf}, {"OEBPS/nav.xhtml", nav},
                             {"OEBPS/text/c1.xhtml", "<html><body><p>Glucose</p></body></html>"}}, true));
        const QVariantList sections = Workspaces::epubSections(epub);
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections[0].toMap().value("label").toString(), QString("Chapter 1 Energy"));
        QCOMPARE(sections[1].toMap().value("depth").toInt(), 1);
        QCOMPARE(sections[1].toMap().value("target").toString(), QString("OEBPS/text/c1.xhtml#s1"));

        Workspaces spaces(nullptr, nullptr, dir.filePath("spaces"));
        QSignalSpy changed(&spaces, &Workspaces::changed);
        const QString id = spaces.create("Biology 101");
        QVERIFY(!id.isEmpty());
        const QString pdf = writeBookPdf(dir.filePath("lecture.pdf"), {"Cell respiration\nGlycolysis first", "Krebs cycle"});
        spaces.addDecks(id, {pdf});
        QTRY_VERIFY(spaces.importing().isEmpty() && spaces.workspace(id).value("decks").toList().size() == 1);
        QVariantMap deck = spaces.workspace(id).value("decks").toList().first().toMap();
        QCOMPARE(deck.value("kind").toString(), QString("pdf"));
        QCOMPARE(deck.value("slides").toList().size(), 2);
        QVERIFY(deck.value("slides").toList()[0].toMap().value("title").toString().contains("Cell respiration"));
        // A copy: the original can go away.
        QFile::remove(pdf);
        QVERIFY(QFileInfo::exists(QUrl(deck.value("pdf").toString()).toLocalFile()));
        const QString deckId = deck.value("id").toString();
        spaces.addLink(id, deckId, 0, {{"bookId", "b1"}, {"target", 4}, {"label", "5.3 Glycolysis"}});
        spaces.addLink(id, deckId, 0, {{"bookId", "b1"}, {"target", 4}, {"label", "5.3 Glycolysis"}});   // once only
        deck = spaces.workspace(id).value("decks").toList().first().toMap();
        QCOMPARE(deck.value("slides").toList()[0].toMap().value("links").toList().size(), 1);
        QCOMPARE(spaces.workspace(id).value("books").toStringList(), QStringList{"b1"});
        spaces.removeLink(id, deckId, 0, 0);
        QCOMPARE(spaces.workspace(id).value("decks").toList().first().toMap().value("slides").toList()[0].toMap()
                     .value("links").toList().size(), 0);
        // Study reviews come from Word, LibreOffice, saved web pages and text,
        // as Markdown: headings, bullets and bold kept.
        const QByteArray docx = "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
                                "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Exam 2</w:t></w:r></w:p>"
                                "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/></w:numPr></w:pPr><w:r><w:rPr><w:b/></w:rPr>"
                                "<w:t>Amdahl's law</w:t></w:r><w:r><w:t xml:space=\"preserve\"> and speedup</w:t></w:r></w:p>"
                                "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>Efficiency</w:t></w:r></w:p>"
                                "</w:body></w:document>";
        write(dir.filePath("review.docx"), makeZip({{"word/document.xml", docx}}, true));
        QCOMPARE(Workspaces::documentText(dir.filePath("review.docx")),
                 QString("# Exam 2\n- **Amdahl's law** and speedup\n  - Efficiency"));
        write(dir.filePath("review.html"), "<html><head><title>x</title></head><body><h2>Exam&nbsp;2</h2><p>Know <b>these</b>:</p>"
                                           "<ul><li>OpenMP</li><li>CUDA &amp; warps</li></ul></body></html>");
        QCOMPARE(Workspaces::documentText(dir.filePath("review.html")), QString("## Exam 2\nKnow **these**:\n- OpenMP\n- CUDA & warps"));
        const QByteArray odt = "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
                               "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"><office:body><office:text>"
                               "<text:h text:outline-level=\"1\">Review</text:h><text:list><text:list-item><text:p>Memory "
                               "<text:span>coalescing</text:span></text:p></text:list-item></text:list>"
                               "</office:text></office:body></office:document-content>";
        write(dir.filePath("review.odt"), makeZip({{"content.xml", odt}}, true));
        QCOMPARE(Workspaces::documentText(dir.filePath("review.odt")), QString("# Review\n- Memory coalescing"));

        // The user's notes and the explanations watched are kept.
        spaces.setSlideNote(id, deckId, 1, "Ask about warps");
        QCOMPARE(spaces.workspace(id).value("decks").toList().first().toMap().value("slides").toList()[1].toMap()
                     .value("myNote").toString(), QString("Ask about warps"));
        spaces.addHistory(id, deckId, 0, "");
        spaces.addHistory(id, deckId, 1, "Why?");
        spaces.addHistory(id, deckId, 0, "");   // once per slide and question, newest kept
        const QVariantList history = spaces.workspace(id).value("history").toList();
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.last().toMap().value("slide").toInt(), 0);
        // Folders: a deck goes in, the folder is renamed, then removed;
        // the deck stays.
        spaces.setDeckFolder(id, deckId, "GPU unit");
        QCOMPARE(spaces.workspace(id).value("decks").toList().first().toMap().value("folder").toString(), QString("GPU unit"));
        spaces.renameFolder(id, "GPU unit", "CUDA");
        QCOMPARE(spaces.workspace(id).value("decks").toList().first().toMap().value("folder").toString(), QString("CUDA"));
        spaces.removeFolder(id, "CUDA");
        QCOMPARE(spaces.workspace(id).value("decks").toList().size(), 1);
        QCOMPARE(spaces.workspace(id).value("decks").toList().first().toMap().value("folder").toString(), QString());
        QCOMPARE(spaces.list().size(), 1);
        spaces.remove(id);
        QCOMPARE(spaces.list().size(), 0);
    }

    // LaTeX that lost its backslashes in a JSON answer gets them back, and
    // ordinary tabs and newlines are left alone.
    void latexIsRepaired() {
        QCOMPARE(Ollama::repairLatex(QString("$") + QChar(0x0c) + "rac{1}{2}$"), QString("$\\frac{1}{2}$"));
        QCOMPARE(Ollama::repairLatex("$a \times b$ and $\theta$"), QString("$a \\times b$ and $\\theta$"));
        QCOMPARE(Ollama::repairLatex("$\nu = \rho$"), QString("$\\nu = \\rho$"));
        QCOMPARE(Ollama::repairLatex("Line one\nnot maths\tthere"), QString("Line one\nnot maths\tthere"));
        QCOMPARE(Ollama::repairLatex("$x$\nnext"), QString("$x$\nnext"));
    }

    // Updates: versions compare as numbers, the right package is picked for
    // the system, and checksums are read in the format sha256sum writes.
    void updaterReadsReleases() {
        QVERIFY(Updater::compareVersions("v0.10.0", "0.9.3") > 0);
        QCOMPARE(Updater::compareVersions("1.2", "1.2.0"), 0);
        QVERIFY(Updater::compareVersions("0.1.0", "0.1.1") < 0);
        const auto asset = [](const char *name) {
            return QJsonObject{{"name", name}, {"browser_download_url", "https://example.org/x"}, {"size", 10}};
        };
        const QJsonObject release{{"tag_name", "v0.2.0"}, {"body", "New: graph"}, {"html_url", "https://example.org/r"},
                                  {"assets", QJsonArray{asset("shelf-debug-0.2.0-1-x86_64.pkg.tar.zst"),
                                                        asset("shelf-0.2.0-1-x86_64.pkg.tar.zst"),
                                                        asset("shelf-0.2.0-setup.exe"), asset("SHA256SUMS")}}};
        const Updater::Release r = Updater::parseRelease(QJsonDocument(release).toJson());
        QVERIFY(r.valid);
        QCOMPARE(r.version, QString("0.2.0"));
        QCOMPARE(Updater::pickAsset(r.assets, "arch", "x86_64"), 1);   // not the debug symbols
        QCOMPARE(Updater::pickAsset(r.assets, "arch", "aarch64"), -1);
        QCOMPARE(Updater::pickAsset(r.assets, "windows", "x86_64"), 2);
        QCOMPARE(Updater::pickAsset(r.assets, "", "x86_64"), -1);
        QVERIFY(!Updater::parseRelease(QJsonDocument(QJsonObject{{"tag_name", "nightly"}}).toJson()).valid);
        const auto sums = Updater::parseChecksums(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789ABCDEF  shelf-0.2.0-setup.exe\n"
            "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210 *dist/shelf.pkg.tar.zst\nnot a line\n");
        QCOMPARE(sums.size(), 2);
        QCOMPARE(sums.value("shelf-0.2.0-setup.exe"), QString("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        QVERIFY(sums.contains("shelf.pkg.tar.zst"));
    }

    // A chapter's text: from its page (or EPUB file) up to the next entry's.
    void chapterTextCoversItsRange() {
        QList<BookSearch::Unit> pages{{0, "", "zero"}, {1, "", "one"}, {2, "", "two"}, {3, "", "three"}};
        QCOMPARE(BookSearch::rangeText(pages, 1, 3).simplified(), QString("one two"));
        QCOMPARE(BookSearch::rangeText(pages, 2, QString()).simplified(), QString("two three"));
        QCOMPARE(BookSearch::rangeText(pages, 2, 2).simplified(), QString("two"));
        QList<BookSearch::Unit> epub{{0, "", "cover", "cover.xhtml"}, {1, "", "first", "text/ch1.xhtml"},
                                     {2, "", "second", "text/ch2.xhtml"}};
        QCOMPARE(BookSearch::rangeText(epub, "text/ch1.xhtml#start", "text/ch2.xhtml").simplified(), QString("first"));
    }

    void quizAnswersBecomeCards() {
        const QVariantList found = Cards::parseQuiz(
            "Here are three flashcards:\n\n**Q:** What does ATP stand for?\n**A:** Adenosine triphosphate.\n\n"
            "1. **Q:** Where is ATP made?\n**A:** In the mitochondria,\nmostly by ATP synthase.\n\n"
            "Q: A question with no answer\n\n**Q:** Last one?\n**A:** Yes.");
        QCOMPARE(found.size(), 3);
        QCOMPARE(found[0].toMap()["front"].toString(), QString("What does ATP stand for?"));
        QCOMPARE(found[1].toMap()["back"].toString(), QString("In the mitochondria, mostly by ATP synthase."));
        QCOMPARE(found[2].toMap()["back"].toString(), QString("Yes."));
    }

    // SM-2: new cards a day, then six, then growing by their ease; a
    // forgotten card comes back in minutes and grows more slowly after.
    void cardsFollowSpacedRepetition() {
        QTemporaryDir dir;
        const QString file = dir.filePath("cards.json");
        const qint64 day = 24LL * 3600 * 1000, start = 1700000000000LL;
        {
            Cards cards(file);
            cards.setNow(start);
            QCOMPARE(cards.addFromQuiz("b1", "**Q:** One?\n**A:** 1\n\n**Q:** Two?\n**A:** 2"), 2);
            QCOMPARE(cards.addFromQuiz("b1", "**Q:** one?\n**A:** again"), 0);   // already there
            QCOMPARE(cards.dueCount(), 2);
            const QString id = cards.due()[0].toMap()["id"].toString();
            QCOMPARE(cards.preview(id, Cards::Good), QString("1 d"));
            QCOMPARE(cards.preview(id, Cards::Again), QString("10 min"));
            cards.grade(id, Cards::Good);
            QCOMPARE(cards.dueCount(), 1);
            cards.setNow(start + day);
            QCOMPARE(cards.preview(id, Cards::Good), QString("6 d"));
            cards.grade(id, Cards::Good);
            cards.setNow(start + 7 * day);
            QCOMPARE(cards.preview(id, Cards::Good), QString("15 d"));   // 6 × 2.5
            cards.grade(id, Cards::Again);
            QCOMPARE(cards.preview(id, Cards::Good), QString("1 d"));
            QVERIFY(cards.list("b1")[0].toMap()["ease"].toDouble() < 2.5 || cards.list("b1")[1].toMap()["ease"].toDouble() < 2.5);
        }
        // Saved and read back.
        Cards reread(file);
        QCOMPARE(reread.totalCount(), 2);
        QCOMPARE(reread.count("b1"), 2);
    }

    void englishPluralsShip() {
        Translator translator(QTemporaryDir().path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.translate("LibraryPage", "%n book(s)", nullptr, 1), QString("%n book"));
        QCOMPARE(translator.translate("LibraryPage", "%n book(s)", nullptr, 3), QString("%n books"));
    }
};

QTEST_MAIN(ShelfTests)
#include "shelf_tests.moc"
