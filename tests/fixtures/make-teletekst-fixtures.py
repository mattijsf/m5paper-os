#!/usr/bin/env python3
"""Writes the Teletekst HTTP fixtures of the UI and unit tests (docs/apps/teletekst.md): pages in the shape
teletekst-data.nos.nl/json/<page> answers (checked against the live service on 2026-11-18): 25 lines of 40 cells, spans with
colour classes ("yellow bg-blue "), links (<a class="yellow" href="#104">), mosaics as private-use character references
(&#xF020; to &#xF07f;), named references for accents. The text is made up: no page of the real service is copied.

Pages: 100 (index), 101 (headlines), 104 (an article), 702 with a second subpage (a map of mosaics, a table) and 199, which
does not exist (404).

  python3 -I tests/fixtures/make-teletekst-fixtures.py tests/fixtures/http
"""
import html, json, os, sys

HOST = "teletekst-data.nos.nl"
COLS, ROWS = 40, 25
LINE = 0x2C   # the middle row of blocks: a horizontal line
FULL = 0x7F   # all six blocks
NAMED = {"é": "&eacute;", "ë": "&euml;", "ï": "&iuml;", "ó": "&oacute;", "&": "&amp;", "<": "&lt;", ">": "&gt;"}


class Page:
    def __init__(self, number):
        self.number = number
        # a cell: [text or mosaic code, foreground, background, link]
        self.cells = [[[" ", "white", None, None] for _ in range(COLS)] for _ in range(ROWS)]
        self.text(0, 21, " NOS Teletekst", "green")
        self.text(0, 35, " ", "yellow")
        self.text(0, 36, str(number), "yellow", link=str(number))
        self.text(0, 39, " ", "yellow")

    def text(self, row, col, s, fg="white", bg=None, link=None):
        for i, ch in enumerate(s):
            if 0 <= col + i < COLS:
                self.cells[row][col + i] = [ch, fg, bg, link]

    def mosaic(self, row, col, codes, fg="white", bg=None):
        for i, code in enumerate(codes):
            if 0 <= col + i < COLS:
                self.cells[row][col + i] = [code, fg, bg, None]

    def fill(self, row, col, n, bg, fg=None):
        for i in range(n):
            self.cells[row][col + i] = [" ", fg or bg, bg, None]

    def fast(self, titles):
        colours = ["red", "green", "yellow", "cyan"]
        col = 1
        for (title, _), colour in zip(titles, colours):
            self.text(24, col, title, colour)
            col += len(title) + 3

    def content(self):
        lines = []
        for row in self.cells:
            out, i = "", 0
            while i < COLS:
                _, fg, bg, link = row[i]
                j = i
                while j < COLS and row[j][1:] == [fg, bg, link]:
                    j += 1
                piece = ""
                for ch, *_ in row[i:j]:
                    piece += "&#xF0%02x;" % ch if isinstance(ch, int) else NAMED.get(ch, ch)
                if link:
                    piece = '<a class="%s" href="#%s">%s</a>' % (fg, link, piece)
                    if fg != "white" or bg:
                        piece = '<span class="%s %s">%s</span>' % (fg, "bg-%s " % bg if bg else "", piece)
                elif fg != "white" or bg:
                    piece = '<span class="%s %s">%s</span>' % (fg, "bg-%s " % bg if bg else "", piece)
                out += piece
                i = j
            lines.append(out)
        return "\n".join(lines) + "\n"


def write(out, ref, page, prev="", next_="", prev_sub="", next_sub="", fast=()):
    page.fast(fast)
    body = {"prevPage": prev, "nextPage": next_, "prevSubPage": prev_sub, "nextSubPage": next_sub,
            "fastTextLinks": [{"title": t, "page": p} for t, p in fast], "content": page.content()}
    with open(os.path.join(out, "GET_%s_json_%s.json" % (HOST, ref)), "w") as f:
        json.dump(body, f, ensure_ascii=False)
        f.write("\n")


def index():
    p = Page(100)
    for r in range(1, 7):
        p.fill(r, 1, 39, "blue", "white")
    p.mosaic(1, 3, [FULL] * 36, "white", "blue")
    p.mosaic(2, 3, [FULL] * 4, "white", "blue")
    p.mosaic(2, 35, [FULL] * 4, "white", "blue")
    p.text(3, 9, "T E L E T E K S T", "white", "blue")
    p.text(4, 9, "voorbeeldpagina's", "white", "blue")
    p.mosaic(5, 3, [FULL] * 4, "white", "blue")
    p.mosaic(5, 35, [FULL] * 4, "white", "blue")
    p.mosaic(6, 3, [FULL] * 36, "white", "blue")
    p.mosaic(7, 1, [0x23] * 39, "white")
    heads = [("Gemeente plant duizend bomen", 104), ("Fietsbrug weer open na herstel", 105),
             ("Recordoogst voor appeltelers", 106), ("Café heropent na verbouwing", 107)]
    for r in range(8, 16):
        p.fill(r, 1, 39, "white", "white")
    for i, (title, number) in enumerate(heads):
        row = 8 + 2 * i
        p.text(row, 3, title + "." * (32 - len(title)), "blue", "white")
        p.text(row, 36, str(number), "blue", "white", link=str(number))
    p.mosaic(16, 1, [0x23] * 39, "blue")
    for r in range(17, 22):
        p.fill(r, 1, 39, "blue", "blue")
    left = [("nieuws", 101), ("televisie", 200), ("radio", 250), ("financieel", 501)]
    right = [("sport", 600), ("weer,verkeer", 700), ("voetbal", 800), ("ondertiteling", 888)]
    for i in range(4):
        for col, (title, number) in ((5, left[i]), (22, right[i])):
            p.text(17 + i, col, title, "white", "blue")
            p.text(17 + i, col + (11 if col == 5 else 14), str(number), "white", "blue", link=str(number))
    p.mosaic(22, 1, [0x23] * 39, "blue")
    p.fill(23, 1, 39, "blue", "blue")
    p.text(23, 8, "gemaakt voor de tests 2026", "white", "blue")
    return p


def headlines():
    p = Page(101)
    square = [0x20, 0x7C, 0x2C, 0x7C]
    p.mosaic(1, 1, square, "white")
    p.mosaic(1, 5, square, "red")
    p.mosaic(1, 9, square, "white")
    p.mosaic(1, 13, [0x20] + [LINE] * 26, "blue")
    p.mosaic(2, 1, [0x20, 0x7F, 0x20, 0x7F], "white")
    p.mosaic(2, 5, [0x20, 0x7F, 0x20, 0x7F], "red")
    p.mosaic(2, 9, [0x20, 0x2F, 0x2C, 0x7C], "white")
    p.fill(2, 14, 26, "blue")
    p.text(2, 15, " N  I  E  U  W  S", "white", "blue")
    p.mosaic(3, 1, [0x20, 0x2F, 0x20, 0x2F], "white")
    p.mosaic(3, 5, [0x20, 0x2F, 0x2C, 0x2F], "red")
    p.mosaic(3, 9, [0x20, 0x2C, 0x2C, 0x2F], "white")
    p.mosaic(3, 13, [0x20] + [0x7C] + [LINE] * 25, "blue")
    p.mosaic(4, 0, [0x20] + [0x23] * 14, "blue")
    heads = [("Gemeente plant duizend bomen", 104), ("Fietsbrug weer open na herstel", 105),
             ("Recordoogst voor appeltelers", 106), ("Café heropent na verbouwing", 107),
             ("Kunst & cultuur krijgen meer geld", 108), ("Molen draait weer na storm", 109),
             ("Veerdienst vaart vaker in de zomer", 110), ("Bibliotheek blijft langer open", 111),
             ('"Zeehond gered door strandwacht"', 112)]
    for i, (title, number) in enumerate(heads):
        row = 5 + 2 * i
        p.text(row, 1, title + "." * (35 - len(title)) + " ", "cyan")
        p.text(row, 37, str(number), "yellow", link=str(number))
    p.fill(23, 1, 39, "blue", "blue")
    p.text(23, 3, "nieuwsoverzicht 102,103 - voorbeeld", "white", "blue")
    return p


def article():
    p = Page(104)
    p.mosaic(1, 0, [0x20] + [LINE] * 39, "yellow")
    p.fill(2, 1, 39, "blue", "blue")
    p.text(2, 4, "Gemeente plant duizend bomen", "yellow", "blue")
    p.mosaic(3, 0, [0x20] + [LINE] * 39, "yellow")
    p.mosaic(4, 0, [0x20, 0x2F], "yellow")
    text = [
        " De gemeente plant deze winter duizend",
        "bomen langs de fietspaden.Het gaat om",
        "eiken,linden en beuken.De eerste boom",
        "gaat zaterdag de grond in bij het",
        "station.",
        "",
        "Bewoners mochten vorig jaar meedenken",
        "over de plekken.Er kwamen ruim",
        "driehonderd ideeën binnen.De meeste",
        "wensen gingen over meer schaduw op",
        "schoolpleinen en bij speeltuinen.",
        "",
        "De wethouder noemt het plan een mooie",
        "eerste stap.In het voorjaar volgen",
        "nog tweehonderd fruitbomen in de",
        "wijken aan de rand van de stad.",
        "",
        "Wie een boom wil adopteren,kan zich",
        "melden bij het loket in het stadhuis.",
    ]
    for i, line in enumerate(text):
        p.text(4 + i, 2 if i == 0 else 1, line.strip() if i == 0 else line, "cyan")
    p.mosaic(23, 0, [0x20] + [LINE] * 39, "yellow")
    return p


def weather_map():
    p = Page(702)
    p.fill(1, 1, 39, "blue", "blue")
    p.text(1, 3, "VERWACHTING TOT MIDDERNACHT:", "yellow", "blue")
    p.text(1, 36, "1/2", "cyan", "blue")
    # A made-up island: an outline in cyan with the land in green.
    import math
    for row in range(3, 23):
        for col in range(17, 39):
            dx, dy = (col - 28) / 10.5, (row - 12.5) / 9.5
            d = math.hypot(dx, dy) + 0.12 * math.sin(col * 0.9) * math.cos(row * 0.7)
            if d < 0.8:
                p.mosaic(row, col, [FULL], "green")
            elif d < 0.97:
                p.mosaic(row, col, [0x7F if (row + col) % 3 else 0x6A], "cyan")
    for row, col, value in ((6, 27, "10"), (11, 22, "11"), (12, 31, "10"), (17, 26, "12"), (19, 33, "11")):
        p.text(row, col - 1, " " + value + " ", "red")
    for i, line in enumerate(["Veel bewolking", "met af en toe", "een bui,vooral", "in het midden.", "In de middag", "ook wat zon."]):
        p.text(6 + i, 1, line, "cyan")
    p.text(20, 3, "=", "red")
    p.text(20, 5, "max.temp.", "green")
    p.text(21, 5, "vandaag", "green")
    p.mosaic(23, 1, [0x23] * 11, "blue")
    p.text(23, 13, "voorbeeldkaart", "green")
    p.mosaic(23, 29, [0x23] * 11, "blue")
    return p


def weather_table():
    p = Page(702)
    p.fill(1, 1, 39, "blue", "blue")
    p.text(1, 3, "VERWACHTING VOOR MORGEN:", "yellow", "blue")
    p.text(1, 36, "2/2", "cyan", "blue")
    p.text(3, 1, "plaats          weer        min  max", "yellow")
    places = [("Den Helder", "bewolkt", 6, 10), ("Groningen", "regen", 5, 9), ("Zwolle", "buien", 5, 10),
              ("Utrecht", "wolkenveld", 6, 11), ("Vlissingen", "zonnig", 7, 12), ("Maastricht", "mist", 4, 11)]
    for i, (place, sky, lo, hi) in enumerate(places):
        p.text(5 + 2 * i, 1, "%-15s %-11s %3d  %3d" % (place, sky, lo, hi), "white")
    p.text(18, 1, "Wind:zuidwest,matig tot vrij krachtig.", "cyan")
    p.text(19, 1, "Aan zee kans op een enkele windstoot.", "cyan")
    p.mosaic(23, 1, [0x23] * 39, "blue")
    return p


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/http"
    news = [("nieuws", "101"), ("sport", "601"), ("tv gids", "200"), ("weer", "702")]
    inside = [("nieuws", "101"), ("binnenland", "102"), ("buitenland", "103"), ("sport", "601")]
    write(out, "100", index(), next_="101", fast=news)
    write(out, "101", headlines(), prev="100", next_="102", fast=[("binnenland", "102"), ("sport", "601"), ("verkeer", "730"), ("weer", "702")])
    write(out, "104", article(), prev="103", next_="105", fast=inside)
    write(out, "702", weather_map(), prev="701", next_="703", next_sub="702-2", fast=inside)
    write(out, "702-1", weather_map(), prev="701", next_="703", next_sub="702-2", fast=inside)
    write(out, "702-2", weather_table(), prev="701", next_="703", prev_sub="702-1", fast=inside)
    # A page that does not exist: 404 with an empty body.
    open(os.path.join(out, "GET_%s_json_199.json" % HOST), "w").close()
    with open(os.path.join(out, "GET_%s_json_199.status" % HOST), "w") as f:
        f.write("404\n")


if __name__ == "__main__":
    main()
