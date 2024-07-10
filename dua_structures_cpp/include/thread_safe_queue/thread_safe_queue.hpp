/**
 * Class template for a thread-safe queue.
 *
 * Roberto Masocco <r.masocco@dotxautomation.com>
 *
 * July 10, 2024
 */

/**
 * Copyright 2024 dotX Automation s.r.l.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DUA_STRUCTURES_CPP__THREAD_SAFE_QUEUE_HPP_
#define DUA_STRUCTURES_CPP__THREAD_SAFE_QUEUE_HPP_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>

namespace dua_structures_cpp
{

/**
 * Thread-safe, many-to-one queue.
 */
template<typename DataT>
class ThreadSafeQueue
{
public:
  /**
   * @brief Constructor.
   *
   * @param cleaner Cleaner to invoke on each element when the queue is cleared.
   */
  explicit ThreadSafeQueue(std::function<void(DataT &)> && cleaner = nullptr)
  : cleaner_(cleaner)
  {
    alarm_.store(false, std::memory_order_release);
  }

  /**
   * @brief Destructor, clears the queue and cleans up.
   */
  ~ThreadSafeQueue()
  {
    clear();
  }

  /* This class is non-copyable and non-moveable. */
  ThreadSafeQueue(const ThreadSafeQueue &) = delete;
  ThreadSafeQueue(ThreadSafeQueue &&) = delete;
  ThreadSafeQueue & operator=(const ThreadSafeQueue &) = delete;
  ThreadSafeQueue & operator=(ThreadSafeQueue &&) = delete;

  /**
   * @brief Clears the queue, cleaning up elements as instructed.
   */
  void clear()
  {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!queue_.empty()) {
      DataT element = queue_.front();
      queue_.pop();
      if (cleaner_) {
        cleaner_(element);
      }
    }
  }

  /**
   * @brief Pushes a new element into the queue.
   *
   * @param item Element to push.
   */
  void push(DataT item)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      queue_.push(item);
    }
    cond_.notify_one();
  }

  /**
   * @brief Pops an element from the queue, blocking until one is available or a wakeup is forced.
   *
   * @return The popped element.
   *
   * @throws LogicError if a thread is already waiting on the queue.
   * @throws RuntimeError if a wakeup is forced while waiting.
   */
  DataT pop()
  {
    std::unique_lock<std::mutex> lock(mutex_);

    // Get out if a thread is already waiting
    if (waiting_ > 0U) {
      throw std::logic_error("ThreadSafeQueue::pop: Thread already waiting on this queue");
    }

    // Wait for an element to be available or for a wakeup
    waiting_++;
    cond_.wait(
      lock,
      [this]() -> bool
      {
        return !queue_.empty() || alarm_.load(std::memory_order_acquire);
      });
    waiting_--;

    // If this wakeup was forced, get out
    if (alarm_.load(std::memory_order_acquire)) {
      alarm_.store(false, std::memory_order_release);
      throw std::runtime_error("ThreadSafeQueue::pop: Forced wakeup");
    }

    // Retrieve and return the next element
    DataT front_item = queue_.front();
    queue_.pop();
    return front_item;
  }

  /**
   * @brief Wakes up the waiting thread, if any.
   */
  void notify()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      alarm_.store(true, std::memory_order_release);
    }
    cond_.notify_one();
  }

private:
  /* Underlying queue. */
  std::queue<DataT> queue_;

  /* Mutex to synchronize access to the queue. */
  std::mutex mutex_;

  /* Condition variable to wait for new elements. */
  std::condition_variable cond_;

  /* Waiting threads counter. */
  unsigned int waiting_ = 0U;

  /* Flag used to force wake up of waiting threads. */
  std::atomic<bool> alarm_;

  /* Cleaner to invoke on each element. */
  std::function<void(DataT &)> cleaner_;
};

} // namespace dua_structures_cpp

#endif // DUA_STRUCTURES_CPP__THREAD_SAFE_QUEUE_HPP_
