#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <queue>

template <typename T>
class BlockingQueue {
public:
    BlockingQueue(bool nonblock = false) : nonblock_(nonblock) {}
    // Enqueue
    void Push(const T &value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        not_empty_.notify_one();
    }

    // Dequeue; returns false if cancelled while empty
    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || nonblock_; });
        if (queue_.empty())
            return false;

        value = queue_.front();
        queue_.pop();
        return true;
    }

    // Wake waiters and stop blocking on an empty queue
    void Cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        nonblock_ = true;
        not_empty_.notify_all();
    }
private:
    bool nonblock_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable not_empty_;
};

template <typename T>
class BlockingQueuePro {
public:
    BlockingQueuePro(bool nonblock = false) : nonblock_(nonblock) {}
    // Enqueue
    void Push(const T &value) {
        std::lock_guard<std::mutex> lock(producer_mutex_);
        producer_queue_.push(value);
        not_empty_.notify_one();
    }

    // Dequeue; returns false if cancelled while empty
    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(consumer_mutex_);
        if (consumer_queue_.empty() && SwapQueue_() == 0) {
            return false;
        }
        value = consumer_queue_.front();
        consumer_queue_.pop();
        return true;
    }

    // Wake waiters and stop blocking on an empty queue
    void Cancel() {
        std::lock_guard<std::mutex> lock(producer_mutex_);
        nonblock_ = true;
        not_empty_.notify_all();
    }

private:

    // When the consumer queue is empty, swap with the producer queue
    int SwapQueue_() {
        std::unique_lock<std::mutex> lock(producer_mutex_);
        not_empty_.wait(lock, [this] { return !producer_queue_.empty() || nonblock_; });
        std::swap(producer_queue_, consumer_queue_);
        return consumer_queue_.size();
    }

    bool nonblock_;
    std::queue<T> producer_queue_;
    std::queue<T> consumer_queue_;
    std::mutex producer_mutex_;
    std::mutex consumer_mutex_;
    std::condition_variable not_empty_;
};

class ThreadPool {
public:
    // Construct and start worker threads
    explicit ThreadPool(int num_threads) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { Worker(); });
        }
    }

    // Stop the pool and join workers
    ~ThreadPool() {
        task_queue_.Cancel();
        for (auto &worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }

    // Post a task to the pool.
    // F is a callable (function, lambda, functor, ...); Args are its argument types.
    // Args... is a parameter pack: any number of arguments.
    template<typename F, typename... Args>
    void Post(F &&f, Args &&...args) {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        task_queue_.Push(task);
    }

private:
    // Worker thread entry
    void Worker() {
        while (true) {
            std::function<void()> task;
            if (!task_queue_.Pop(task))
                break;
            task();
        }
    }

    BlockingQueue<std::function<void()>> task_queue_;   // task queue
    std::vector<std::thread> workers_;                  // worker threads
};