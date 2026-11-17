// Background worker for blocking jobs (HTTP, EPUB indexing). One std::thread, FIFO queue.
// Rule (docs/ARCHITECTURE.md "Threads"): a job never touches the canvas or widgets. Its result is
// delivered to `onDone` on the UI thread, from Worker::runCompleted() in the main loop.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

namespace pos {

class Worker {
 public:
  Worker() = default;
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  // Starts the thread. On the device call it after esp_pthread_set_cfg() set the stack size.
  void start();
  // Called from the worker thread when a result is ready (simulator: wakes the event wait).
  void setWakeup(std::function<void()> fn) { wakeup_ = std::move(fn); }

  // Runs `job` on the worker; then `onDone(result)` on the UI thread. Capture by value in `job`.
  // If cancelAll() was called in between, `onDone` never runs. `job` may return void.
  template <class Job, class Done>
  void async(Job job, Done onDone) {
    using R = std::invoke_result_t<Job>;
    if constexpr (std::is_void_v<R>) {
      enqueue([job = std::move(job)]() mutable { job(); }, [onDone = std::move(onDone)]() mutable { onDone(); });
    } else {
      auto slot = std::make_shared<std::optional<R>>();
      enqueue([slot, job = std::move(job)]() mutable { slot->emplace(job()); },
              [slot, onDone = std::move(onDone)]() mutable { onDone(std::move(**slot)); });
    }
  }

  // Runs `job` on the worker whatever happens meanwhile: cancelAll() does not skip it, and nothing comes back. For work
  // that must be done even if the one who asked is gone by then (a file to save, something to switch off again).
  // Capture by value, and nothing that dies with a screen or an app.
  template <class Job>
  void post(Job job) {
    enqueue([job = std::move(job)]() mutable { job(); }, nullptr);
  }

  // UI thread: runs the callbacks of finished jobs, oldest first. Returns how many ran.
  int runCompleted();
  // Drops everything not yet delivered: queued jobs are skipped, finished results are discarded.
  // (A job that is running right now finishes, but its callback is dropped.) Used when an app closes.
  void cancelAll();
  // Jobs submitted and not yet delivered or dropped.
  size_t pending() const { return pending_.load(); }
  // True if results are waiting for runCompleted().
  bool hasCompleted();

 private:
  struct Task {
    uint32_t generation;
    std::function<void()> run;
    std::function<void()> deliver;  // none: a post(), which cancelAll() leaves alone
  };
  void enqueue(std::function<void()> run, std::function<void()> deliver);
  void loop();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Task> queue_;
  std::deque<Task> done_;
  std::atomic<uint32_t> generation_{0};
  std::atomic<size_t> pending_{0};
  bool stop_ = false;
  std::function<void()> wakeup_;
};

}  // namespace pos
