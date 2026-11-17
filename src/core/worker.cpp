#include "core/worker.h"

namespace pos {

Worker::~Worker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void Worker::start() {
  if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
}

void Worker::enqueue(std::function<void()> run, std::function<void()> deliver) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(Task{generation_.load(), std::move(run), std::move(deliver)});
    ++pending_;
  }
  cv_.notify_one();
}

void Worker::loop() {
  for (;;) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    if (task.deliver && task.generation != generation_.load()) {  // cancelled while queued
      --pending_;
      continue;
    }
    task.run();
    if (!task.deliver) {  // a post(): nothing to hand to the UI thread
      --pending_;
      if (wakeup_) wakeup_();
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      done_.push_back(std::move(task));
    }
    if (wakeup_) wakeup_();
  }
}

bool Worker::hasCompleted() {
  std::lock_guard<std::mutex> lock(mutex_);
  return !done_.empty();
}

int Worker::runCompleted() {
  std::deque<Task> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ready.swap(done_);
  }
  int ran = 0;
  for (Task& t : ready) {
    --pending_;
    if (t.generation != generation_.load()) continue;  // cancelled: drop
    t.deliver();
    ++ran;
  }
  return ran;
}

void Worker::cancelAll() {
  ++generation_;  // queued jobs are skipped by the thread; finished ones are dropped by runCompleted()
}

}  // namespace pos
