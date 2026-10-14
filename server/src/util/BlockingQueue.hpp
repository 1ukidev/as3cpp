#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace as3lsp::util {

template <typename T> class BlockingQueue {
public:
    void push(T value) {
        {
            std::scoped_lock lock{mutex_};
            items_.push_back(std::move(value));
        }
        ready_.notify_one();
    }

    [[nodiscard]] T pop() {
        std::unique_lock lock{mutex_};
        ready_.wait(lock, [this] { return !items_.empty(); });
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

    // Waits at most `timeout` for an item. Returns nullopt when the time runs out.
    [[nodiscard]] std::optional<T> popFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        if (!ready_.wait_for(lock, timeout, [this] { return !items_.empty(); })) {
            return std::nullopt;
        }
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> items_;
};

} // namespace as3lsp::util
