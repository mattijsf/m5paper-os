// The agenda from the phone (core/agenda, apps/agenda/day_layout; docs/COMPANION.md 7.1 `agenda`, D28).
#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "apps/agenda/day_layout.h"
#include "core/agenda.h"
#include "core/companion.h"
#include "core/fs.h"

using namespace pos;
using namespace pos::agenda;

namespace {

struct Roots {
  std::string dir;
  Roots() {
    char tmpl[] = "/tmp/pos-agenda-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/data");
  }
  ~Roots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

const int kThu = daysFromCivil(2026, 1, 15);

const std::string kRun1 = R"({"t":"agenda","from":"2026-01-15","to":"2026-01-17","days":[
  {"d":0,"ev":[{"n":"Birthday"},{"s":570,"e":615,"n":"Dentist","l":"Main St 4"},{"s":780,"e":840,"n":"Lunch"}]},
  {"d":2,"ev":[{"s":1320,"n":"Night train"}],"more":2}]})";

Event ev(int s, int e, const char* n = "x") {
  Event x;
  x.start = s;
  x.end = e;
  x.title = n;
  return x;
}

}  // namespace

TEST_CASE("agenda: civil dates round-trip, weekdays, parsing rejects bad dates") {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(formatDate(kThu) == "2026-01-15");
  CHECK(weekday(kThu) == 4);
  int d = 0;
  CHECK(parseDate("2028-02-29", d));
  CHECK(formatDate(d) == "2028-02-29");
  CHECK_FALSE(parseDate("2026-02-29", d));
  CHECK_FALSE(parseDate("2026-1-15", d));
  CHECK_FALSE(parseDate("2026-13-01", d));
  for (int x = -1000; x < 100000; x += 37) {
    int y, m, dd;
    civilFromDays(x, y, m, dd);
    CHECK(daysFromCivil(y, m, dd) == x);
  }
}

TEST_CASE("agenda: a run replaces every day it covers, and only those") {
  Agenda a;
  REQUIRE(apply(a, kRun1, 1768480000));
  CHECK(a.received);
  CHECK(a.updated == 1768480000);
  REQUIRE(a.days.size() == 2);
  CHECK(a.days[0].date == kThu);
  REQUIRE(a.days[0].events.size() == 3);
  CHECK(a.days[0].events[0].allDay());
  CHECK(a.days[0].events[1].location == "Main St 4");
  CHECK(a.days[1].date == kThu + 2);
  CHECK(a.days[1].more == 2);
  CHECK(a.days[1].events[0].end == -1);
  // Friday to Sunday: Saturday is now empty, Sunday new, Thursday untouched.
  REQUIRE(apply(a, R"({"t":"agenda","from":"2026-01-16","to":"2026-01-18","days":[{"d":2,"ev":[{"s":600,"e":660,"n":"Brunch"}]}]})", 0));
  REQUIRE(a.days.size() == 2);
  CHECK(a.days[0].date == kThu);
  CHECK(a.days[1].date == kThu + 3);
  CHECK(a.updated == 0);
  // The phone switched sharing off: one empty run over the window empties the Paper.
  REQUIRE(apply(a, R"({"t":"agenda","from":"2026-01-15","to":"2026-01-28","days":[]})", 0));
  CHECK(a.days.empty());
  CHECK(a.received);
}

TEST_CASE("agenda: malformed runs and events are refused") {
  Agenda a;
  CHECK_FALSE(apply(a, "not json", 0));
  CHECK_FALSE(apply(a, R"({"t":"clip","text":"x"})", 0));
  CHECK_FALSE(apply(a, R"({"t":"agenda","from":"2026-01-17","to":"2026-01-15","days":[]})", 0));       // to before from
  CHECK_FALSE(apply(a, R"({"t":"agenda","from":"2026-01-01","to":"2026-01-15","days":[]})", 0));       // 15 days
  CHECK_FALSE(apply(a, R"({"t":"agenda","from":"2026-02-30","to":"2026-03-01","days":[]})", 0));
  CHECK_FALSE(a.received);
  // Bad events are skipped, a day outside the run or listed twice is ignored, at most 12 events.
  std::string many;
  for (int i = 0; i < 20; ++i) many += std::string(i ? "," : "") + R"({"s":)" + std::to_string(i * 60) + R"(,"n":"e"})";
  REQUIRE(apply(a, R"({"t":"agenda","from":"2026-01-15","to":"2026-01-16","days":[
    {"d":0,"ev":[{"s":-5,"n":"neg"},{"s":600,"e":500,"n":"backwards"},{"s":"9","n":"string"},{"s":60,"e":1440,"n":"ok"}]},
    {"d":0,"ev":[{"n":"second listing"}]},{"d":5,"ev":[{"n":"outside"}]},{"d":1,"ev":[)" + many + "]}]}",
                0));
  REQUIRE(a.days.size() == 2);
  REQUIRE(a.days[0].events.size() == 1);
  CHECK(a.days[0].events[0].title == "ok");
  CHECK(a.days[1].events.size() == static_cast<size_t>(kMaxEventsPerDay));
}

TEST_CASE("agenda: saved and loaded, expired before today, forgotten with the pairing") {
  Roots roots;
  Agenda a;
  REQUIRE(apply(a, kRun1, 1768480000));
  REQUIRE(save(a));
  Agenda b;
  REQUIRE(load(b));
  CHECK(b.received);
  CHECK(b.updated == 1768480000);
  REQUIRE(b.days.size() == 2);
  CHECK(b.days[0].events[1].start == 570);
  CHECK(b.days[1].more == 2);
  CHECK(expire(b, kThu + 1));
  CHECK(b.days.size() == 1);
  CHECK_FALSE(expire(b, kThu + 1));
  companion::forgetPairing();
  Agenda c;
  CHECK_FALSE(load(c));
  CHECK_FALSE(c.received);
}

TEST_CASE("agenda: texts for times, days and the next line") {
  CHECK(timeText(ev(-1, -1)) == "All day");
  CHECK(timeText(ev(570, 615)) == "09:30 - 10:15");
  CHECK(timeText(ev(1320, -1)) == "From 22:00");
  CHECK(timeText(ev(-1, 840)) == "Until 14:00");
  CHECK(timeText(ev(540, 540)) == "09:00");
  CHECK(dayTitle(kThu, kThu) == "Today");
  CHECK(dayTitle(kThu + 1, kThu) == "Tomorrow");
  CHECK(dayTitle(kThu + 2, kThu) == "Saturday 17 January");

  Agenda a;
  REQUIRE(apply(a, kRun1, 0));
  CHECK(nextLine(a, kThu, 8 * 60) == "09:30 Dentist");
  CHECK(nextLine(a, kThu, 9 * 60 + 40) == "Now: Dentist until 10:15");
  CHECK(nextLine(a, kThu, 13 * 60 + 34) == "Now: Lunch until 14:00");
  CHECK(nextLine(a, kThu, 15 * 60) == "Today: Birthday");             // only the all-day event is left today
  CHECK(nextLine(a, kThu + 1, 600) == "Tomorrow 22:00 Night train");  // Friday is empty
  CHECK(nextLine(a, kThu - 2, 600) == "Thu 15 Jan: Birthday");
  CHECK(nextLine(a, kThu + 3, 600) == "");
  CHECK(nextLine(Agenda{}, kThu, 600) == "");
}

TEST_CASE("agenda: the day grid widens around the events and splits overlapping blocks into columns") {
  Day d;
  CHECK(windowFor(d).from == 8 * 60);
  CHECK(windowFor(d).to == 20 * 60);
  d.events = {ev(-1, -1, "all day"), ev(7 * 60 + 30, 8 * 60), ev(21 * 60, 21 * 60 + 10)};
  Window w = windowFor(d);
  CHECK(w.from == 7 * 60);
  CHECK(w.to == 22 * 60);
  d.events.push_back(ev(23 * 60, -1, "late"));
  CHECK(windowFor(d).to == 24 * 60);

  Day o;  // 09:00-11:00 and 10:00-10:30 overlap, 12:00-13:00 alone; 09:00 all-day gets no block
  o.events = {ev(9 * 60, 11 * 60, "a"), ev(10 * 60, 10 * 60 + 30, "b"), ev(12 * 60, 13 * 60, "c"), ev(-1, -1, "d")};
  w = windowFor(o);
  const auto blocks = layoutBlocks(o, w, 1200, 34);  // 100 px per hour
  REQUIRE(blocks.size() == 3);
  CHECK(blocks[0].event == 0);
  CHECK(blocks[0].top == 100);
  CHECK(blocks[0].bottom == 300);
  CHECK(blocks[0].cols == 2);
  CHECK(blocks[1].event == 1);
  CHECK(blocks[1].col == 1);
  CHECK(blocks[1].cols == 2);
  CHECK(blocks[2].cols == 1);
  CHECK(blocks[2].col == 0);
  // A short event gets the minimum height, also at the bottom of the grid.
  Day s;
  s.events = {ev(19 * 60 + 55, 20 * 60)};
  const auto sb = layoutBlocks(s, windowFor(s), 600, 34);
  REQUIRE(sb.size() == 1);
  CHECK(sb[0].bottom == 600);
  CHECK(sb[0].bottom - sb[0].top == 34);
}

TEST_CASE("agenda: `agenda` envelopes reach the caller in counter order") {
  using namespace pos::companion;
  Pairing p;
  KeyPair phone;
  for (int i = 0; i < 32; ++i) p.sk[i] = static_cast<uint8_t>(i), phone.sk[i] = static_cast<uint8_t>(0x40 + i);
  publicKeyOf(p.sk, p.pk);
  publicKeyOf(phone.sk, phone.pk);
  for (int i = 0; i < 8; ++i) p.pid[i] = static_cast<uint8_t>(0xa1 + i);
  p.phonePk = phone.pk;
  p.paired = true;
  REQUIRE(deriveSession(p.sk, phone.pk, p.pid, p.pk, phone.pk, p.session));
  const std::string second = R"({"t":"agenda","from":"2026-01-16","to":"2026-01-16","days":[]})";
  const std::string q = std::string(R"({"state":"1","attributes":{"queue":[)") + R"({"id":2,"ts":1,"e":")" +
                        seal(p.session.p2d, kDirP2D, p.pid, 2, second) + R"("},{"id":1,"ts":1,"e":")" +
                        seal(p.session.p2d, kDirP2D, p.pid, 1, kRun1) + R"("}]}})";
  Notifs n;
  ApplyResult r = applyQueue(p, n, q, 1768480000);
  CHECK(r.accepted == 2);
  REQUIRE(r.agenda.size() == 2);
  CHECK(r.agenda[0] == kRun1);
  CHECK(r.agenda[1] == second);
  Agenda a;
  for (const auto& plain : r.agenda) CHECK(apply(a, plain, 0));
  REQUIRE(a.days.size() == 2);  // the second run covered only the (empty) Friday: Thursday and Saturday stay
}
