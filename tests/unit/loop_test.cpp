#include <doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "core/timers.h"
#include "core/worker.h"

using namespace pos;

namespace {

class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return 0; }
  void set(int64_t) override {}
  uint32_t ms = 1000;
};

// Waits (real time, bounded) until the worker has results, then delivers them on this thread.
int drain(Worker& w, size_t expect, int timeoutMs = 2000) {
  int ran = 0;
  for (int waited = 0; ran < static_cast<int>(expect) && waited < timeoutMs; ++waited) {
    ran += w.runCompleted();
    if (ran < static_cast<int>(expect)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ran;
}

}  // namespace

TEST_CASE("timers fire in due order, ties in insertion order") {
  FakeClock clock;
  Timers t(clock);
  std::vector<std::string> order;
  t.after(300, [&] { order.push_back("c300"); });
  t.after(100, [&] { order.push_back("a100"); });
  t.after(100, [&] { order.push_back("b100"); });
  t.after(200, [&] { order.push_back("d200"); });
  CHECK(t.nextDeadlineMs() == 1100);

  clock.ms += 99;
  CHECK(t.runDue() == 0);
  clock.ms += 1;
  CHECK(t.runDue() == 2);
  CHECK(t.nextDeadlineMs() == 1200);
  clock.ms += 1000;  // everything else is overdue: still earliest first
  CHECK(t.runDue() == 2);
  CHECK(order == std::vector<std::string>{"a100", "b100", "d200", "c300"});
  CHECK(t.nextDeadlineMs() == kNoDeadline);
}

TEST_CASE("timers: cancel, periodic, and callbacks that add timers") {
  FakeClock clock;
  Timers t(clock);
  int a = 0, p = 0, chained = 0;
  auto id = t.after(50, [&] { ++a; });
  CHECK(t.cancel(id));
  CHECK_FALSE(t.cancel(id));
  auto per = t.every(100, [&] { ++p; });
  clock.ms += 100;
  t.runDue();
  clock.ms += 100;
  t.runDue();
  CHECK(p == 2);
  CHECK(a == 0);
  CHECK(t.cancel(per));
  clock.ms += 1000;
  CHECK(t.runDue() == 0);

  // after(0) inside a callback waits for the next runDue(): no spinning.
  t.after(0, [&] { t.after(0, [&] { ++chained; }); });
  CHECK(t.runDue() == 1);
  CHECK(chained == 0);
  CHECK(t.runDue() == 1);
  CHECK(chained == 1);
}

TEST_CASE("timers: a periodic timer does not catch up after a long stall") {
  FakeClock clock;
  Timers t(clock);
  int n = 0;
  t.every(100, [&] { ++n; });
  clock.ms += 1000;  // ten periods late
  t.runDue();
  CHECK(n == 1);
  CHECK(t.nextDeadlineMs() == clock.ms + 100);
}

TEST_CASE("timers survive the 32-bit millisecond wrap") {
  FakeClock clock;
  clock.ms = 0xFFFFFF00u;
  Timers t(clock);
  int n = 0;
  t.after(0x200, [&] { ++n; });  // due after the wrap
  clock.ms = 0xFFFFFFFFu;
  CHECK(t.runDue() == 0);
  clock.ms = 0x00000101u;
  CHECK(t.runDue() == 1);
}

TEST_CASE("worker: the job runs off-thread, the result arrives on the calling thread") {
  Worker w;
  w.start();
  const auto ui = std::this_thread::get_id();
  std::thread::id jobThread, doneThread;
  int got = 0;
  w.async(
      [&] {
        jobThread = std::this_thread::get_id();
        return 41 + 1;
      },
      [&](int v) {
        doneThread = std::this_thread::get_id();
        got = v;
      });
  CHECK(w.pending() == 1);
  CHECK(drain(w, 1) == 1);
  CHECK(got == 42);
  CHECK(jobThread != ui);
  CHECK(doneThread == ui);
  CHECK(w.pending() == 0);
}

TEST_CASE("worker: results are delivered only from runCompleted, in submission order") {
  Worker w;
  w.start();
  std::vector<int> seen;
  for (int i = 0; i < 5; ++i) w.async([i] { return i; }, [&seen](int v) { seen.push_back(v); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(seen.empty());  // finished, but nothing is delivered until the UI thread asks
  CHECK(drain(w, 5) == 5);
  CHECK(seen == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("worker: void jobs and strings work") {
  Worker w;
  w.start();
  std::atomic<bool> ran{false};
  bool done = false;
  std::string text;
  w.async([&] { ran = true; }, [&] { done = true; });
  w.async([] { return std::string("hello"); }, [&](std::string s) { text = s; });
  CHECK(drain(w, 2) == 2);
  CHECK(ran);
  CHECK(done);
  CHECK(text == "hello");
}

TEST_CASE("worker: a cancelled generation drops the callback") {
  Worker w;
  w.start();
  std::atomic<bool> release{false};
  bool firstDelivered = false, secondDelivered = false;
  // The first job blocks until released; the second waits behind it in the queue.
  w.async(
      [&] {
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 1;
      },
      [&](int) { firstDelivered = true; });
  w.async([] { return 2; }, [&](int) { secondDelivered = true; });
  w.cancelAll();  // the app closed: nothing submitted so far may call back
  release = true;

  bool thirdDelivered = false;
  w.async([] { return 3; }, [&](int) { thirdDelivered = true; });  // new generation: delivered
  CHECK(drain(w, 1) == 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  w.runCompleted();
  CHECK_FALSE(firstDelivered);
  CHECK_FALSE(secondDelivered);
  CHECK(thirdDelivered);
  CHECK(w.pending() == 0);
}

TEST_CASE("worker: a posted job runs even when everything else was cancelled") {
  Worker w;
  w.start();
  std::atomic<bool> release{false};
  std::atomic<bool> skipped{false}, saved{false};
  bool delivered = false;
  // A job blocks the queue; behind it one whose result a screen waits for, and one that must happen all the same.
  w.async([&] { while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }, [] {});
  w.async([&] { skipped = true; return 1; }, [&](int) { delivered = true; });
  w.post([&] { saved = true; });
  w.cancelAll();  // the app closed
  release = true;
  for (int i = 0; i < 500 && w.pending() > 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    w.runCompleted();
  }
  CHECK(saved);          // the file is written, the radio is switched back
  CHECK_FALSE(skipped);  // what only the closed screen wanted is not even started
  CHECK_FALSE(delivered);
  CHECK(w.pending() == 0);
}

TEST_CASE("worker: wakeup is called from the worker thread when a result is ready") {
  Worker w;
  std::atomic<int> wakes{0};
  w.setWakeup([&] { ++wakes; });
  w.start();
  w.async([] { return 7; }, [](int) {});
  CHECK(drain(w, 1) == 1);
  CHECK(wakes >= 1);
}

TEST_CASE("worker: destructor with pending jobs does not hang") {
  auto w = std::make_unique<Worker>();
  w->start();
  for (int i = 0; i < 3; ++i) w->async([] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); return 0; }, [](int) {});
  w.reset();  // joins
  CHECK(true);
}
