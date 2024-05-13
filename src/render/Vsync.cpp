#include <chrono>
#include <Vsync.hpp>

void Vsync::start() {
    worker = thread(&Vsync::run, this);
}

void Vsync::stop() {
    isCancelled.store(true);
}

void Vsync::run() {
    while (!isCancelled.load()) {
        this_thread::sleep_for(chrono::milliseconds(32));

        function<void()> cb;
        {
            std::lock_guard<std::mutex> guard(mtx);
            cb = this->cb;
        }
        cb();
    }
}

void Vsync::callback(const function<void()>& callback) {
    lock_guard<std::mutex> guard(mtx);
    this->cb = callback;
}