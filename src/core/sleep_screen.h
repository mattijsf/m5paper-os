// The screen left on the panel at power-off (docs/POWER.md "Sleep screen"): the book being read with its cover and progress, or the
// PaperOS mark; under it the state in words, "Powered off" with the power icon, and "Press the side button to start" (the panel
// keeps its picture without power: nothing else tells off from on). No clock: it stays for hours.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/agenda.h"
#include "core/app.h"
#include "core/gfx.h"

namespace pos {

// The Today board (POWER.md "Sleep screen", task P7-16): what the sleep screen shows of the day when there is something
// to show. Plain text and icons: core draws it, the parts are filled from the saved agenda (core/agenda) and, through
// the weather source, from the weather app's saved copy. Forecasts and clock times only: the screen stays for hours.
struct SleepBoard {
  std::string date;  // "Thursday 15 January": the day the board was drawn on
  // Weather: today's forecast and the next blocks of three hours.
  struct Block {
    std::string label;  // "15–18"
    Icon icon = Icon::Cloud;
    std::string temp, rain;  // "7°", "70%" ("" when dry)
  };
  bool weather = false;
  Icon weatherIcon = Icon::Cloud;
  std::string temps;    // "7° / 1°": high and low
  std::string summary;  // "Rain, 80%"
  std::vector<Block> blocks;  // at most kSleepBlocks
  // Agenda: today's events that are not over, then the next days' under a heading with the day's name.
  struct Row {
    bool heading = false;
    std::string time, title;  // heading: the day in `title`
    bool operator==(const Row& o) const { return heading == o.heading && time == o.time && title == o.title; }
  };
  std::vector<Row> agenda;
  bool empty() const { return !weather && agenda.empty(); }
};
constexpr int kSleepBlocks = 4, kSleepAgendaRows = 6;

// The agenda rows of the board at `today` / `minute` (core/agenda localNow): at most `maxRows` lines, headings included,
// never ending on a heading. Pure.
std::vector<SleepBoard::Row> sleepAgendaRows(const agenda::Agenda& a, int today, int minute, int maxRows = kSleepAgendaRows);
// Fills the weather part of a board for `now` (the weather app's saved copy; no network). Set once by the composition
// root (apps/apps.cpp): core does not include apps.
using SleepWeatherSource = void (*)(SleepBoard& board, int64_t now);
void setSleepWeatherSource(SleepWeatherSource source);
// The board for `now` from what is saved on the device. Empty when the clock is not set.
SleepBoard buildSleepBoard(int64_t now);
// Draws the whole canvas: the board, the open book as one row (`info`, may be null; `thumb` its cover at kSleepThumbW x
// kSleepThumbH, may be null), the unread counts per app and `updated` ("14:05", "" for none), then "Powered off".
// `on`: the device stays on (idle on USB, D38): "Tap to start" instead of "Powered off" and the side button.
void drawSleepBoard(Canvas& c, const SleepBoard& board, const SleepInfo* info, Canvas* thumb, const std::string& updated, bool on = false);
// What the sleep screen would show now, as text: the same text means the same picture (the Shell redraws a sleep screen
// that stays on only when this changes).
std::string sleepScreenKey(const SleepInfo* info, int64_t now, bool board, const std::string& updated);

// The read of the network a sleep screen that stays on does now and then (the relay, the weather, articles: what a
// scheduled wake reads). Made on the UI thread from what it needs; the returned job runs on the worker and says whether
// anything answered. Set once by the composition root (apps/apps.cpp): core does not include apps.
class Os;
using SleepReadJob = std::function<bool()>;
using SleepReadMaker = SleepReadJob (*)(Os& os);
void setSleepReadMaker(SleepReadMaker maker);
SleepReadMaker sleepReadMaker();

// What power-off and the scheduled refresh call: the board when `board` is set (config power.sleepBoard) and there is
// something for it, else the plain screen below. Loads the cover itself.
void drawSleepScreenFor(Canvas& c, const SleepInfo* info, int64_t now, bool board, const std::string& updated, bool on = false);

// Draws the whole canvas. `info` may be null (PaperOS mark); `image` is the decoded picture of info->image (may be null).
// `note` is one caption line above the hint ("3 unread - updated 14:05"), "" for none; `agenda` is one line above it: what
// the phone's agenda has next ("Tomorrow 09:30 Dentist", core/agenda nextLine), "" for none.
void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image, const std::string& note = "", const std::string& agenda = "", bool on = false);
// The agenda line for the sleep screen, from the saved agenda and the clock (no network). The screen stays for hours: the
// line names what comes next at the moment it is drawn (power-off, a scheduled refresh).
std::string sleepAgendaLine(int64_t now);

// "3 unread - updated 14:05"; parts that are not there are left out.
std::string sleepNote(int unread, const std::string& updated);
// Sum of the launcher badges the apps cached (/data/cache/<id>/badge): no network. Messages only: the Read app's badge
// (articles saved for later, P7-18) is left out.
int cachedUnread();
// The sleep info of the last power-off, kept in /data/system.json so a scheduled refresh can redraw the same screen.
void saveSleepInfo(const SleepInfo* info);
bool loadSleepInfo(SleepInfo& info);

constexpr int kSleepImageW = 240, kSleepImageH = 360;
constexpr int kSleepThumbW = 54, kSleepThumbH = 81;

}  // namespace pos
