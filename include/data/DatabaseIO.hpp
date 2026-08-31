#pragma once
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <future>
#include "core/sqlite3.h"

extern sqlite3* db;

class DatabaseIO {
public:
    static DatabaseIO& get() {
        static DatabaseIO instance;
        return instance;
    }

    void enqueue(std::function<void()> task);
    
    template<class F, class... Args>
    auto enqueue_with_future(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    void wait_idle();

private:
    DatabaseIO();
    ~DatabaseIO();

    void worker_loop();

    std::thread worker;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};
    std::atomic<int> active_tasks{0};
};
