// Teletekst (docs/apps/teletekst.md): the page JSON into a grid of cells, page references, the link a touch means, the
// saved copy, and the font that has to keep the grid.
#include <doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "apps/teletekst/teletekst_model.h"
#include "core/gfx.h"
#include "ui/text.h"

using namespace pos::teletekst;

namespace {
std::string readFixture(const std::string& page) {
  std::ifstream f("tests/fixtures/http/GET_teletekst-data.nos.nl_json_" + page + ".json");
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

TEST_CASE("teletekst: page references") {
  PageRef r;
  CHECK(parseRef("104", r));
  CHECK(r.page == 104);
  CHECK(r.sub == 0);
  CHECK(r.id() == "104");
  CHECK(parseRef("#702/2", r));
  CHECK(r.page == 702);
  CHECK(r.sub == 2);
  CHECK(r.id() == "702-2");
  CHECK(parseRef("702-12", r));
  CHECK(r.sub == 12);
  CHECK(pageUrl(r) == "https://teletekst-data.nos.nl/json/702-12");
  for (const char* bad : {"", "#", "99", "099", "900", "1040", "10a", "104-", "104-x", "104/2/3", "104 ", "abc", "104-100"}) {
    PageRef keep{555, 5};
    CHECK_FALSE(parseRef(bad, keep));
    CHECK(keep == PageRef{555, 5});  // untouched when it fails
  }
}

TEST_CASE("teletekst: markup into cells") {
  Page p;
  parseContent("ab<span class=\"yellow bg-blue \">cd<a class=\"cyan\" href=\"#104\">104</a>e</span>f\n"
               "<a class=\"null\" href=\"#702/2\">x</a>&eacute;&#xE9;&#233;\xC3\xA9&amp;&lt;&unknown; & b\n"
               "&#xF020;&#xF02c;&#xF07f;&#xF021;&#xF060;<span class=\"red \">&#xF07F;</span>\n",
               p);
  // Plain text is white on black.
  CHECK(p.cells[0][0].ch == 'a');
  CHECK(p.cells[0][0].fg == White);
  CHECK(p.cells[0][0].bg == Black);
  CHECK(p.cells[0][0].link == 0);
  // A span sets both colours; a link inside it has its own text colour and keeps the background.
  CHECK(p.cells[0][2].fg == Yellow);
  CHECK(p.cells[0][2].bg == Blue);
  CHECK(p.cells[0][4].ch == '1');
  CHECK(p.cells[0][4].fg == Cyan);
  CHECK(p.cells[0][4].bg == Blue);
  CHECK(p.cells[0][4].link == 104);
  CHECK(p.cells[0][6].link == 104);
  // After the link the span's colours are back, after the span the plain ones.
  CHECK(p.cells[0][7].ch == 'e');
  CHECK(p.cells[0][7].fg == Yellow);
  CHECK(p.cells[0][7].link == 0);
  CHECK(p.cells[0][8].ch == 'f');
  CHECK(p.cells[0][8].fg == White);
  CHECK(p.cells[0][8].bg == Black);
  CHECK(p.cells[0][9].ch == ' ');
  // A link to a subpage; the class "null" changes nothing.
  CHECK(p.cells[1][0].link == 702);
  CHECK(p.cells[1][0].linkSub == 2);
  CHECK(p.cells[1][0].fg == White);
  // Character references, named and numbered, and plain UTF-8: one cell each.
  for (int c = 1; c <= 4; ++c) CHECK(p.cells[1][c].ch == 0xE9);
  CHECK(p.cells[1][5].ch == '&');
  CHECK(p.cells[1][6].ch == '<');
  CHECK(p.cells[1][7].ch == '?');  // a name that is not known
  CHECK(p.cells[1][8].ch == ' ');
  CHECK(p.cells[1][9].ch == '&');  // a bare ampersand
  CHECK(p.cells[1][11].ch == 'b');
  CHECK(rowText(p, 1).substr(0, 1) == "x");
  // Mosaics: a bit per block, the sixth block is bit 6 of the teletext code.
  CHECK(p.cells[2][0].mosaic);
  CHECK(p.cells[2][0].ch == 0);
  CHECK(p.cells[2][1].ch == 0x0C);  // the middle row
  CHECK(p.cells[2][2].ch == 0x3F);  // all six
  CHECK(p.cells[2][3].ch == 0x01);  // top left
  CHECK(p.cells[2][4].ch == 0x20);  // bottom right
  CHECK(p.cells[2][5].mosaic);
  CHECK(p.cells[2][5].fg == Red);
  CHECK(p.cells[2][5].ch == 0x3F);
  CHECK_FALSE(p.cells[2][6].mosaic);
  CHECK(rowText(p, 2) == std::string(kCols, ' '));
}

TEST_CASE("teletekst: more than 40 cells or 25 rows, broken markup") {
  Page p;
  std::string text = std::string(60, 'x') + "\n";
  for (int i = 0; i < 40; ++i) text += "row\n";
  parseContent(text, p);
  CHECK(rowText(p, 0) == std::string(kCols, 'x'));
  CHECK(rowText(p, kRows - 1).substr(0, 3) == "row");
  // A tag that never ends, closing tags without an opening one, an unfinished reference: no crash, the text before stays.
  parseContent("ok</span></a>&#xZZ;&#;<span class=\"red", p);
  CHECK(p.cells[0][0].ch == 'o');
  CHECK(p.cells[0][2].ch == '?');
  CHECK(p.cells[0][3].ch == '?');
  parseContent("", p);
  CHECK(rowText(p, 0) == std::string(kCols, ' '));
}

TEST_CASE("teletekst: the index page of the fixtures") {
  Page p;
  REQUIRE(parsePage(readFixture("100"), PageRef{100, 0}, p));
  CHECK(p.self == PageRef{100, 0});
  CHECK_FALSE(p.prev.valid());
  CHECK(p.next == PageRef{101, 0});
  CHECK_FALSE(p.hasSubpages());
  REQUIRE(p.fast.size() == 4);
  CHECK(p.fast[0].title == "nieuws");
  CHECK(p.fast[0].to == PageRef{101, 0});
  CHECK(p.fast[3].title == "weer");
  CHECK(p.fast[3].to == PageRef{702, 0});
  CHECK(rowText(p, 0) == "                      NOS Teletekst 100 ");
  CHECK(rowText(p, 14).find("Caf\xC3\xA9 heropent") != std::string::npos);
  // The banner: white blocks on blue.
  CHECK(p.cells[1][3].mosaic);
  CHECK(p.cells[1][3].bg == Blue);
  // A headline row: the number at its end is the link; a touch anywhere on the row means it.
  CHECK(p.cells[8][36].link == 104);
  CHECK(p.cells[8][5].link == 0);
  CHECK(linkNear(p, 8, 5) == PageRef{104, 0});
  CHECK(linkNear(p, 10, 39) == PageRef{105, 0});
  // The empty row between two headlines: the row above.
  CHECK(linkNear(p, 9, 5) == PageRef{104, 0});
  // Two links on a row: the nearer one.
  CHECK(linkNear(p, 17, 10) == PageRef{101, 0});
  CHECK(linkNear(p, 17, 30) == PageRef{600, 0});
  // Nothing within a row: no link.
  CHECK_FALSE(linkNear(p, 3, 20).valid());
  CHECK_FALSE(linkNear(p, -1, 0).valid());
  CHECK_FALSE(linkNear(p, kRows, 0).valid());
}

TEST_CASE("teletekst: subpages") {
  Page p;
  REQUIRE(parsePage(readFixture("702"), PageRef{702, 0}, p));
  CHECK(p.self == PageRef{702, 1});  // asked without a subpage, and it has them: the first
  CHECK(p.hasSubpages());
  CHECK_FALSE(p.prevSub.valid());
  CHECK(p.nextSub == PageRef{702, 2});
  CHECK(p.prev == PageRef{701, 0});
  REQUIRE(parsePage(readFixture("702-2"), PageRef{702, 2}, p));
  CHECK(p.self == PageRef{702, 2});
  CHECK(p.prevSub == PageRef{702, 1});
  CHECK_FALSE(p.nextSub.valid());
}

TEST_CASE("teletekst: what is not a page") {
  Page p;
  p.self = PageRef{123, 0};
  CHECK_FALSE(parsePage("", PageRef{199, 0}, p));
  CHECK_FALSE(parsePage(readFixture("199"), PageRef{199, 0}, p));  // the 404's empty body
  CHECK_FALSE(parsePage("<html>Not found</html>", PageRef{199, 0}, p));
  CHECK_FALSE(parsePage("{\"prevPage\":\"100\"}", PageRef{199, 0}, p));
  CHECK_FALSE(parsePage("{\"content\":\"\"}", PageRef{199, 0}, p));
  CHECK_FALSE(parsePage("{\"content\":12}", PageRef{199, 0}, p));
  CHECK(p.self == PageRef{123, 0});  // untouched
  // Content alone is a page: links that are missing or not page numbers are simply not there.
  REQUIRE(parsePage("{\"content\":\"hello\",\"nextPage\":\"xyz\",\"fastTextLinks\":[{\"title\":\"a\"},{\"title\":\"b\",\"page\":\"200\"}]}",
                    PageRef{150, 0}, p));
  CHECK(rowText(p, 0).substr(0, 5) == "hello");
  CHECK_FALSE(p.next.valid());
  REQUIRE(p.fast.size() == 1);
  CHECK(p.fast[0].title == "b");
}

TEST_CASE("teletekst: the saved copy") {
  int64_t fetched = 0;
  std::string json;
  CHECK(unpackSaved(packSaved(1768480440, "{\"content\":\"x\\ny\"}"), fetched, json));
  CHECK(fetched == 1768480440);
  CHECK(json == "{\"content\":\"x\\ny\"}");
  CHECK_FALSE(unpackSaved("", fetched, json));
  CHECK_FALSE(unpackSaved("{\"content\":\"x\"}", fetched, json));
  CHECK_FALSE(unpackSaved("abc\n{}", fetched, json));
  CHECK_FALSE(unpackSaved("123\n", fetched, json));
}

TEST_CASE("teletekst: every character of the mono font is 13 px wide") {
  pos::Canvas c;
  c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  c.createSprite(600, 40);
  using pos::ui::Type;
  CHECK(pos::ui::measureText(c, std::string(40, 'M'), Type::Mono) == 520);
  CHECK(pos::ui::measureText(c, "il.,'| 0123456789 WMwm@#%&()", Type::Mono) == 28 * 13);
  CHECK(pos::ui::measureText(c, "\xC3\xA9\xC3\xAB\xC3\xAF\xC3\xB3\xC3\xBC\xC3\x89\xE2\x82\xAC", Type::Mono) == 7 * 13);
  pos::ui::forgetLoadedFont();
}
