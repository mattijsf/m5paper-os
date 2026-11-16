// The screen left on the panel at power-off (docs/POWER.md "Sleep screen"): the book being read with its cover and progress, or the
// PaperOS mark; "Press the side button to wake". No clock: it stays for hours.
#pragma once

#include "core/app.h"
#include "core/gfx.h"

namespace pos {

// Draws the whole canvas. `info` may be null (PaperOS mark); `image` is the decoded picture of info->image (may be null).
// `note` is one caption line above the hint ("3 unread - updated 14:05"), "" for none; `agenda` is one line above it: what
// the phone's agenda has next ("Tomorrow 09:30 Dentist", core/agenda nextLine), "" for none.
void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image, const std::string& note = "", const std::string& agenda = "");
// The agenda line for the sleep screen, from the saved agenda and the clock (no network). The screen stays for hours: the
// line names what comes next at the moment it is drawn (power-off, a scheduled refresh).
std::string sleepAgendaLine(int64_t now);

// "3 unread - updated 14:05"; parts that are not there are left out.
std::string sleepNote(int unread, const std::string& updated);
// Sum of the launcher badges the apps cached (/data/cache/<id>/badge): no network.
int cachedUnread();
// The sleep info of the last power-off, kept in /data/system.json so a scheduled refresh can redraw the same screen.
void saveSleepInfo(const SleepInfo* info);
bool loadSleepInfo(SleepInfo& info);

constexpr int kSleepImageW = 240, kSleepImageH = 360;

}  // namespace pos
