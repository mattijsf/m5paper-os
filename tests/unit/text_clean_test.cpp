#include <doctest.h>

#include <string>

#include "core/text_clean.h"

using namespace pos;

namespace {
std::string html(const std::string& s) {
  CleanOptions o;
  o.html = true;
  return cleanText(s, o);
}
}  // namespace

TEST_CASE("text_clean: entities") {
  CHECK(decodeEntities("a &amp; b &lt;c&gt; &quot;d&quot; &#8217; &#x20AC; &eacute;") == "a & b <c> \"d\" \xE2\x80\x99 \xE2\x82\xAC \xC3\xA9");
  CHECK(decodeEntities("&bogus; &amp &#; &#xZ; &") == "&bogus; &amp &#; &#xZ; &");
  CHECK(decodeEntities("&#0;") == "\xEF\xBF\xBD");
  CHECK(decodeEntities("&#xD800;") == "\xEF\xBF\xBD");
  CHECK(decodeEntities("&#99999999;") == "&#99999999;");
}

TEST_CASE("text_clean: link host names") {
  CHECK(hostOfUrl("https://www.example.com/a/b?x=1#f") == "example.com");
  CHECK(hostOfUrl("http://user:pw@sub.example.org:8080/x") == "sub.example.org");
  CHECK(hostOfUrl("https://example.com") == "example.com");
  CHECK(hostOfUrl("ftp://example.com") == "");
  CHECK(hostOfUrl("not a url") == "");
  CHECK(cleanText("see https://www.example.com/very/long/path?utm=1 now") == "see example.com now");
  CHECK(cleanText("(https://example.com/x), and https://a.b/c.") == "(example.com), and a.b.");
  CleanOptions keep;
  keep.shortenLinks = false;
  CHECK(cleanText("https://example.com/x", keep) == "https://example.com/x");
}

TEST_CASE("text_clean: emoji, control characters and invalid UTF-8") {
  // Emoji and their sequences stay whole (ui/text draws them from the atlas, docs/UI.md "Emoji").
  CHECK(cleanText("Great \xF0\x9F\x98\x80 see you \xE2\x9D\xA4\xEF\xB8\x8F") == "Great \xF0\x9F\x98\x80 see you \xE2\x9D\xA4\xEF\xB8\x8F");
  CHECK(cleanText("a\xF0\x9F\xA6\x84" "b") == "a\xF0\x9F\xA6\x84" "b");
  CHECK(cleanText("ok \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == "ok \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");
  CHECK(cleanText("us \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7!") ==
        "us \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7!");
  CHECK(cleanText("\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF").size() == 28);  // England: tags kept
  CHECK(std::string(emojiFallback(0x1F44D)) == "(+1)");  // the text form for a panel without the atlas
  CHECK(emojiFallback(0x1F984) == nullptr);
  CHECK(cleanText("a\x01" "b\x7F" "c\xC2\x85" "d") == "abcd");
  CHECK(cleanText("bad \xFF\xFE bytes \xC3 end") == "bad bytes end");
  CHECK(cleanText("caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x94 \xE2\x82\xAC" "5") == "caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x80\x94 \xE2\x82\xAC" "5");  // glyphs stay
  CHECK(cleanText("\xE2\x82\xBF bitcoin") == "bitcoin");  // no glyph for the sign
  CHECK(cleanText("") == "");
  CHECK(cleanText("\xEF\xBB\xBF" "x") == "x");
}

TEST_CASE("text_clean: blanks and long words") {
  CHECK(cleanText("a   b\t\tc\r\nd\r\n\r\n\r\n\r\ne  \n  f") == "a b c\nd\n\ne\nf");
  CHECK(cleanText("\n\n  hi  \n\n") == "hi");
  CHECK(cleanText("a\xC2\xA0\xC2\xA0" "b") == "a b");  // no-break spaces
  const std::string longWord(200, 'x');
  const std::string cut = cleanText("start " + longWord + " end");
  CHECK(cut.find("start ") == 0);
  CHECK(cut.size() < 80);
  CHECK(cut.find("\xE2\x80\xA6 end") != std::string::npos);
  // a cut never lands inside a multi-byte character
  std::string accents;
  for (int i = 0; i < 100; ++i) accents += "\xC3\xA9";
  const std::string c2 = cleanText(accents);
  CHECK(c2.size() % 2 == 1);  // an even number of bytes for the letters plus the 3-byte ellipsis
}

TEST_CASE("text_clean: HTML mail") {
  const std::string mail =
      "<html><head><title>x</title><style>p { color: red }</style></head><body>"
      "<p>Hello <b>Anna</b>,</p><p>Your order &amp; invoice&nbsp;#12 is ready.<br>See "
      "<a href=\"https://www.shop.example/o/12?x=1\">https://www.shop.example/o/12?x=1</a></p>"
      "<!-- hidden --><script>alert(1)</script><ul><li>one</li><li>two</li></ul>"
      "<table><tr><td>a</td><td>b</td></tr></table><div>Bye</div></body></html>";
  CHECK(html(mail) == "Hello Anna,\n\nYour order & invoice #12 is ready.\nSee shop.example\n\none\ntwo\na b\nBye");
  CHECK(html("plain text only") == "plain text only");
  CHECK(html("1 < 2 and 3 > 2") == "1 < 2 and 3 > 2");
  CHECK(html("<p>unterminated") == "unterminated");
  CHECK(html("<style>never closed") == "");
  CHECK(html("<<<>>>") == "<<<>>>");
  // plain text is not treated as HTML: tags and entities stay as written
  CHECK(cleanText("a <b>bold</b> &amp; c") == "a <b>bold</b> &amp; c");
}

TEST_CASE("text_clean: never crashes on noise") {
  unsigned x = 5;
  for (int round = 0; round < 50; ++round) {
    std::string s;
    for (int i = 0; i < 400; ++i) {
      x = x * 1664525u + 1013904223u;
      s += static_cast<char>(x >> 24);
    }
    CleanOptions o;
    o.html = round % 2;
    cleanText(s, o);
  }
  std::string tags;
  for (int i = 0; i < 300; ++i) tags += "<div><p>&amp;&#";
  CHECK(html(tags).size() < tags.size());
}
