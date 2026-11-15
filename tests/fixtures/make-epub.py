#!/usr/bin/env python3
"""Builds the reader's fixture EPUBs (deterministic, committed): tests never depend on downloads.

  python3 tests/fixtures/make-epub.py tests/fixtures/books

  fixture3.epub  EPUB 3: nav document, cover-image, three chapters in OEBPS/text/ (deflated), a %20 in an href,
                 headings, bold, italic, entities, line breaks, quote, list, rule, image, comment, style, script.
  fixture2.epub  EPUB 2: NCX, cover through <meta name="cover">, one stored chapter, creator with opf:role.
"""
import sys, zipfile, struct, zlib, os

FIXED = (2026, 1, 1, 0, 0, 0)

def png(w, h, gray):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes([gray]) * w for _ in range(h))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")

def add(z, name, data, deflate=True):
    zi = zipfile.ZipInfo(name, FIXED)
    zi.compress_type = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    zi.external_attr = 0o644 << 16
    z.writestr(zi, data)

XH = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>%s</title><style>p { margin: 0 }</style><script>var x = "<p>not text</p>";</script></head><body>%s</body></html>\n'

LOREM = ("The quiet river runs past old stone houses where children play and the baker opens his shop before dawn. "
         "A long road winds through hills of heather and gorse while the wind carries distant bells. ")

def chapter1():
    return XH % ("One", '<h1>Chapter One</h1>\n<p>It was a <b>bold</b> start, and an <i>italic</i> one: caf&eacute; &amp; cr&#232;me &#x2014; &ldquo;quoted&rdquo; &mdash; and&nbsp;non&#8209;breaking.</p>\n'
                 '<!-- a comment <p>not text</p> -->\n<p>' + LOREM * 6 + '</p>\n<p>First line<br/>second line<br />third line.</p>\n'
                 '<blockquote><p>A quoted paragraph that stands apart from the text around it.</p></blockquote>\n'
                 '<ul><li>first item</li><li>second <em>item</em></li></ul>\n<hr/>\n<p><img src="cover.png" alt="a picture"/></p>\n'
                 '<p>Text after the rule with a <span class="x"><strong>strong <i>and italic</i></strong></span> run.</p>')

def chapter2():
    return XH % ("Two", '<h2 id="top">Second chapter</h2>\n' + ''.join('<p%s>%s%s</p>\n' % (' id="sub"' if i == 15 else '', LOREM * (1 + i % 4), "Paragraph %d." % i) for i in range(1, 30)))

def chapter3():
    return XH % ("Three", '<h1>The End</h1>\n<p>Last words, with a very long word: ' + 'x' * 90 + ' and the end.</p>\n<h3>Afterword</h3>\n<p>Thanks.</p>')

OPF3 = '''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
 <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
  <dc:identifier id="id">urn:uuid:fixture-3</dc:identifier>
  <dc:title>The Fixture &amp; Co</dc:title>
  <dc:creator opf:role="aut">Ann Example</dc:creator>
  <dc:creator opf:role="trl">Not An Author</dc:creator>
  <dc:language>en</dc:language>
 </metadata>
 <manifest>
  <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  <item id="c1" href="text/Chapter%20One.xhtml" media-type="application/xhtml+xml"/>
  <item id="c2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/>
  <item id="c3" href="text/ch3.xhtml" media-type="application/xhtml+xml"/>
  <item id="cover" href="images/cover.png" media-type="image/png" properties="cover-image"/>
  <item id="css" href="style.css" media-type="text/css"/>
 </manifest>
 <spine><itemref idref="c1"/><itemref idref="c2"/><itemref idref="ghost"/><itemref idref="c3"/></spine>
</package>
'''

NAV3 = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body>
<nav epub:type="landmarks"><ol><li><a href="text/ch3.xhtml">Landmark, not the TOC</a></li></ol></nav>
<nav epub:type="toc"><h1>Contents</h1><ol>
<li><a href="text/Chapter%20One.xhtml">Chapter &amp; One</a></li>
<li><a href="text/ch2.xhtml#top">The  Second
 Chapter</a><ol><li><a href="text/ch2.xhtml#sub">A section of two</a></li></ol></li>
<li><a href="text/ch3.xhtml">The End</a></li>
</ol></nav></body></html>
'''

CONTAINER = '<?xml version="1.0"?>\n<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="%s" media-type="application/oebps-package+xml"/></rootfiles></container>\n'

def epub3(path):
    with zipfile.ZipFile(path, "w") as z:
        add(z, "mimetype", "application/epub+zip", deflate=False)
        add(z, "META-INF/container.xml", CONTAINER % "OEBPS/content.opf")
        add(z, "OEBPS/content.opf", OPF3)
        add(z, "OEBPS/nav.xhtml", NAV3)
        add(z, "OEBPS/text/Chapter One.xhtml", chapter1())
        add(z, "OEBPS/text/ch2.xhtml", chapter2())
        add(z, "OEBPS/text/ch3.xhtml", chapter3())
        add(z, "OEBPS/images/cover.png", png(60, 90, 140), deflate=False)
        add(z, "OEBPS/style.css", "p { margin: 0 }\n")

OPF2 = '''<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">
 <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
  <dc:title>Old Style</dc:title>
  <dc:creator opf:role="aut" opf:file-as="Writer, Old">Old Writer</dc:creator>
  <meta name="cover" content="img"/>
 </metadata>
 <manifest>
  <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  <item id="a" href="a.html" media-type="application/xhtml+xml"/>
  <item id="img" href="cov.png" media-type="image/png"/>
 </manifest>
 <spine toc="ncx"><itemref idref="a"/></spine>
</package>
'''

NCX = '''<?xml version="1.0"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>Old Style</text></docTitle>
<navMap><navPoint id="n1" playOrder="1"><navLabel><text>Only chapter</text></navLabel><content src="a.html"/>
<navPoint id="n2" playOrder="2"><navLabel><text>Inside</text></navLabel><content src="a.html#inside"/></navPoint></navPoint></navMap></ncx>
'''

def epub2(path):
    with zipfile.ZipFile(path, "w") as z:
        add(z, "mimetype", "application/epub+zip", deflate=False)
        add(z, "META-INF/container.xml", CONTAINER % "content.opf")
        add(z, "content.opf", OPF2)
        add(z, "toc.ncx", NCX)
        add(z, "a.html", "<html><body><h1>Only</h1><p>Short stored chapter.</p></body></html>", deflate=False)
        add(z, "cov.png", png(30, 45, 90), deflate=False)

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
epub3(os.path.join(out, "fixture3.epub"))
epub2(os.path.join(out, "fixture2.epub"))
