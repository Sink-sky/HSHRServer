#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace sinksky {
    using std::condition_variable;
    using std::lock_guard;
    using std::mutex;
    using std::queue;
    using std::thread;
    using std::unique_lock;
    using std::vector;

    template <typename Restype>
    class threadpool {
      private:
        const int MAX_THREAD_NUM;
        const int MAX_QUEUE_NUM;
        mutex mtx;
        condition_variable notEmpty;
        condition_variable notFull;
        queue<Restype> resQueue;
        vector<unique_ptr<thread>> threadGroup;

      public:
        threadpool(int threadnum, int queuenum)
            : MAX_THREAD_NUM(threadnum), MAX_QUEUE_NUM(queuenum), threadGroup(MAX_THREAD_NUM) {}
        ~threadpool() = default;
        threadpool(const threadpool&) = delete;
        threadpool& operator=(const threadpool&) = delete;

        bool isFull() { return resQueue.size() == MAX_QUEUE_NUM; }

        bool isEmpty() { return resQueue.empty(); }

        void add(Restype res) {
            unique_lock<mutex> locker(mtx);
            notFull.wait(locker, [this] { return !isFull(); });
            resQueue.push(res);
            notEmpty.notify_one();
        }

        Restype take() {
            unique_lock<mutex> locker(mtx);
            notEmpty.wait(locker, [this] { return !isEmpty(); });
            Restype res = resQueue.front();
            resQueue.pop();
            notFull.notify_one();
            return res;
        }

        template <typename Processtype>
        void task() {
            while (true) {
                Restype res = take();
                Processtype(res).process();
            }
        }

        template <typename Processtype>
        void work() {
            for (int i = 0; i < MAX_THREAD_NUM; ++i) {
                threadGroup[i]
                    = std::make_unique<thread>((&threadpool<Restype>::task<Processtype>), this);
                threadGroup[i]->detach();
            }
        }
    };

}  // namespace sinksky