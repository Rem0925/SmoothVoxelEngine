#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    void wait_idle();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    size_t active = 0;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable idle_condition;
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
    if (threads == 0) threads = 4;
    for(size_t i = 0; i<threads; ++i)
        workers.emplace_back(
            [this] {
                for(;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                        ++this->active;
                    }
                    task();
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        --this->active;
                        if (this->active == 0) this->idle_condition.notify_all();
                    }
                }
            }
        );
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

inline void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_condition.wait(lock, [this]{ return tasks.empty() && active == 0; });
}
