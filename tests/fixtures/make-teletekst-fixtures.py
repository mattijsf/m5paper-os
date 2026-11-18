#!/usr/bin/env python3
"""Writes the Teletekst HTTP fixtures of the UI and unit tests (docs/apps/teletekst.md): pages in the shape
teletekst-data.nos.nl/json/<page> answers (checked against the live service on 2026-11-18): 25 lines of 40 cells, spans with
colour classes ("yellow bg-blue "), links (<a class="yellow" href="#104">), mosaics as private-use character references
(&#xF020; to &#xF07f;), named references for accents. The text is made up: no page of the real service is copied.

Pages: 100 (index), 101 (headlines), 104 (an article), 702 with a second subpage (a map of mosaics: our own drawing of the
Netherlands in the service's colours; a table) and 199, which does not exist (404).

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


# The weather map: our own rough drawing of the Netherlands (corner points by hand, degrees east and north), built the
# way the service's map is: the coast as a cyan line, the Wadden Sea, the IJsselmeer and the Westerschelde filled cyan,
# Germany and Belgium green, the North Sea and the land itself black with the temperatures in red on it.
MAP_COL, MAP_ROW, MAP_COLS, MAP_ROWS = 17, 3, 23, 20
WEST, EAST, SOUTH, NORTH = 3.2, 7.3, 50.7, 53.6
LAND = [(3.37, 51.37), (3.45, 51.55), (3.70, 51.75), (3.87, 51.83), (4.05, 51.98), (4.27, 52.11), (4.42, 52.25),
        (4.55, 52.46), (4.62, 52.75), (4.72, 52.96), (5.03, 52.93), (5.40, 53.07), (5.41, 53.17), (5.60, 53.30),
        (6.00, 53.40), (6.20, 53.41), (6.85, 53.46), (6.95, 53.33), (7.20, 53.24), (7.20, 53.00), (7.05, 52.64),
        (6.72, 52.63), (6.70, 52.48), (7.06, 52.40), (7.05, 52.23), (6.83, 51.97), (6.40, 51.83), (5.95, 51.83),
        (6.22, 51.50), (6.08, 51.17), (5.95, 51.05), (6.10, 50.92), (6.02, 50.76), (5.70, 50.76), (5.75, 51.15),
        (5.50, 51.28), (5.05, 51.47), (4.75, 51.43), (4.40, 51.37), (4.22, 51.37), (3.90, 51.22), (3.37, 51.28)]
SEA = [(3.00, 51.25), (3.37, 51.37), (3.45, 51.55), (3.70, 51.75), (3.87, 51.83), (4.05, 51.98), (4.27, 52.11),
       (4.42, 52.25), (4.55, 52.46), (4.62, 52.75), (4.72, 52.96), (4.85, 53.18), (5.05, 53.30), (5.35, 53.42),
       (5.75, 53.47), (6.20, 53.50), (6.85, 53.55), (7.00, 53.55), (7.40, 53.70), (3.00, 53.70)]
WATER = [
    # Wadden Sea
    [(4.72, 52.96), (4.85, 53.18), (5.05, 53.30), (5.35, 53.42), (5.75, 53.47), (6.20, 53.50), (6.85, 53.55),
     (7.00, 53.55), (7.05, 53.42), (7.22, 53.33), (7.20, 53.24), (6.95, 53.33), (6.85, 53.46), (6.20, 53.41),
     (6.00, 53.40), (5.60, 53.30), (5.41, 53.17), (5.40, 53.07), (5.03, 52.93)],
    # IJsselmeer
    [(5.03, 52.93), (5.40, 53.07), (5.72, 52.84), (5.60, 52.66), (5.42, 52.52), (5.20, 52.36), (5.02, 52.37),
     (5.07, 52.64), (5.10, 52.78)],
    # Westerschelde
    [(3.45, 51.42), (3.85, 51.36), (4.22, 51.37), (4.22, 51.43), (3.85, 51.45), (3.50, 51.48)],
]


def inside(poly, x, y):
    hit = False
    for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            hit = not hit
    return hit


def map_pixels():
    """The map as rows of 2 x 3 block pixels: ' ' sea or land, 'c' cyan, 'g' green."""
    w, h = MAP_COLS * 2, MAP_ROWS * 3
    kind = [["" for _ in range(w)] for _ in range(h)]
    for py in range(h):
        for px in range(w):
            x = WEST + (px + 0.5) / w * (EAST - WEST)
            y = NORTH - (py + 0.5) / h * (NORTH - SOUTH)
            if any(inside(poly, x, y) for poly in WATER):
                kind[py][px] = "water"
            elif inside(LAND, x, y):
                kind[py][px] = "land"
            elif inside(SEA, x, y):
                kind[py][px] = "sea"
            else:
                kind[py][px] = "abroad"
    out = [[" " for _ in range(w)] for _ in range(h)]
    for py in range(h):
        for px in range(w):
            k = kind[py][px]
            if k == "water":
                out[py][px] = "c"
            elif k == "abroad":
                out[py][px] = "g"
            elif k == "land":   # the coast: land with the sea beside it
                near = [(px - 1, py), (px + 1, py), (px, py - 1), (px, py + 1)]
                if any(0 <= nx < w and 0 <= ny < h and kind[ny][nx] == "sea" for nx, ny in near):
                    out[py][px] = "c"
    return out


def weather_map():
    p = Page(702)
    p.fill(1, 1, 39, "blue", "blue")
    p.text(1, 3, "VERWACHTING TOT MIDDERNACHT:", "yellow", "blue")
    p.text(1, 36, "1/2", "cyan", "blue")
    pixels = map_pixels()
    for row in range(MAP_ROWS):
        for col in range(MAP_COLS):
            six = [pixels[row * 3 + i // 2][col * 2 + i % 2] for i in range(6)]   # bit i of a block, as in the model
            colour = "c" if six.count("c") >= six.count("g") else "g"   # a cell has one colour: the one with most blocks
            bits = sum(1 << i for i in range(6) if six[i] == colour)
            if bits:
                code = 0x20 | (bits & 0x1F) | ((bits & 0x20) << 1)
                p.mosaic(MAP_ROW + row, MAP_COL + col, [code], "cyan" if colour == "c" else "green")
    for row, col, value in ((4, 20, "11"), (6, 35, "10"), (11, 34, "10"), (13, 28, "11"), (17, 21, "12"), (17, 30, "11")):
        p.text(row, col, value, "red")
    for i, line in enumerate(["Veel bewolking", "met af en toe", "een bui,vooral", "in het midden.", "In de middag", "ook wat zon."]):
        p.text(6 + i, 1, line, "cyan")
    p.text(19, 3, "=", "green")
    p.text(19, 5, "max.temp.", "green")
    p.text(20, 5, "vandaag", "green")
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
