#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <mutex>

using namespace std;

class Vsync {
    private:
        thread worker;
        atomic<bool> isCancelled;
        mutex mtx;
        function<void()> cb;

        void run();
    public:
        void start();
        void stop();
        void callback(const function<void()>& callback);
};