#include "data/DatabaseIO.hpp"

DatabaseIO::DatabaseIO() {
    worker = std::thread(&DatabaseIO::worker_loop, this);
}

DatabaseIO::~DatabaseIO() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_one();
    if (worker.joinable()) {
        worker.join();
    }
}

void DatabaseIO::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.push(std::move(task));
    }
    condition.notify_one();
}

void DatabaseIO::wait_idle() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (tasks.empty() && active_tasks == 0) {
            break;
        }
        lock.unlock();
        std::this_thread::yield();
    }
}

void DatabaseIO::worker_loop() {
    while (true) {
        std::vector<std::function<void()>> batch;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this] { return stop || !tasks.empty(); });
            if (stop && tasks.empty()) {
                return;
            }
            constexpr size_t BATCH_SIZE = 32;
            while (!tasks.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(tasks.front()));
                tasks.pop();
                active_tasks++;
            }
        }

        if (db && batch.size() > 1) {
            sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
        }
        for (auto& task : batch) {
            task();
        }
        if (db && batch.size() > 1) {
            sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        }
        active_tasks -= static_cast<int>(batch.size());
    }
}
