#!/usr/bin/env python3
"""Builds src/learn/shelf-guide.epub, the practice book the Learn course
opens. Run it after changing the text below."""
import pathlib, zipfile

OUT = pathlib.Path(__file__).resolve().parent.parent / "src/learn/shelf-guide.epub"

CHAPTERS = [
    ("Welcome", """
<p>This short book is for practising Shelf. Nothing you do here touches your own books, so try everything: highlight, write notes, bookmark pages, and ask the AI. You can remove this book from your library whenever you like.</p>
<p>Turn pages with the arrow keys, the space bar, or the mouse wheel, or click the left or right edge of the page. Press <b>T</b> to show or hide the outline on the left; it lists this book's chapters, and the one you are reading is marked.</p>
<p>Press <b>?</b> at any time to see every keyboard shortcut.</p>
"""),
    ("Practise selecting and highlighting", """
<p>Drag across this sentence to select it, and a bar appears with four highlight colours.</p>
<p>Double-click the word <i>mitochondria</i> to select just that word. Triple-click anywhere in this sentence to select the whole sentence and nothing more. Then pick a colour from the bar to keep it as a highlight.</p>
<p>Click a highlight later to change its colour, add a note, or remove it. If you remove something by mistake, <b>Ctrl+Z</b> brings it back, and <b>Ctrl+Shift+Z</b> does it again.</p>
<p>The heart pumps blood through two circuits. The pulmonary circuit carries blood to the lungs to pick up oxygen, and the systemic circuit carries oxygen-rich blood to the rest of the body. Cardiac output is the volume of blood the heart pumps each minute: heart rate multiplied by stroke volume.</p>
"""),
    ("Notes and bookmarks", """
<p>Select a passage and press <b>Note</b> to write a note on it. The passage is highlighted and the note is kept with it.</p>
<p>Press <b>B</b> or the bookmark button at the top to bookmark the page you are on; press it again to remove the bookmark.</p>
<p>Open the <b>Notes</b> tab in the sidebar to see everything you have marked in this book, grouped by chapter. Click a chapter's heading to fold it away, search your notes, and use <i>Copy all</i> or <i>Export</i> to take them elsewhere as Markdown.</p>
"""),
    ("Asking the AI", """
<p>Shelf can explain, summarise, or quiz you on any passage, using AI that runs on this computer through Ollama. Nothing leaves your machine. If <b>Local AI</b> in the library's sidebar has a green dot, it is ready.</p>
<p>Select the paragraph below and press <b>Ask AI</b>. Choose <i>Explain</i>, <i>Summarize</i>, or <i>Quiz me</i>, or type your own question. When you like the answer, press <i>Save to notes</i> and it is kept with the passage.</p>
<p>Enzymes are proteins that speed up chemical reactions by lowering their activation energy. Each enzyme has an active site shaped to fit its substrate. A competitive inhibitor resembles the substrate and competes for the active site, so adding more substrate can overcome it; a noncompetitive inhibitor binds elsewhere and changes the enzyme's shape, so more substrate does not help.</p>
"""),
    ("Studying with Shelf", """
<p>Read a chapter through once, highlighting only what you would want to review. Then open the Notes tab, fold the chapters you have finished, and read your highlights back.</p>
<p>For anything that did not stick, select it and choose <i>Quiz me</i>: the flashcards it writes can be saved to your notes and reviewed later.</p>
<p>Right-click a chapter in the outline and choose <i>Practice test on this…</i>. Pick <b>Hands-on</b> and Shelf chooses an exercise that fits: a function it runs against tests, a Markov chain, a molecule to draw, or a formula to explore with sliders. Your code runs in a sandbox, never on your computer itself.</p>
<p>Write [[a concept]] in a note, such as [[glycolysis]], and the <b>Knowledge graph</b> in the library joins every highlight about it, across all your books.</p>
<p>For a PDF that has no outline, such as a scanned book, open its Outline tab and press <b>Build outline</b>. Shelf reads every page and lists the chapters and sections as it finds them, while you keep reading. In the <b>View</b> menu, <i>Clean up scanned pages</i> makes yellowed scans easier to read.</p>
"""),
]

def page(title, body):
    return f"""<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head>
<body><h1>{title}</h1>{body}</body></html>"""

with zipfile.ZipFile(OUT, "w") as z:
    z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip")
    z.writestr("META-INF/container.xml", """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>""")
    items = "".join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(len(CHAPTERS)))
    spine = "".join(f'<itemref idref="c{i}"/>' for i in range(len(CHAPTERS)))
    z.writestr("OEBPS/book.opf", f"""<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">shelf-guide</dc:identifier>
<dc:title>Practising with Shelf</dc:title><dc:creator>Shelf</dc:creator><dc:language>en</dc:language></metadata>
<manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>{items}</manifest>
<spine>{spine}</spine></package>""")
    links = "".join(f'<li><a href="c{i}.xhtml">{t}</a></li>' for i, (t, _) in enumerate(CHAPTERS))
    z.writestr("OEBPS/nav.xhtml", f"""<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head>
<body><nav epub:type="toc"><ol>{links}</ol></nav></body></html>""")
    for i, (title, body) in enumerate(CHAPTERS):
        z.writestr(f"OEBPS/c{i}.xhtml", page(title, body))
print(OUT)
