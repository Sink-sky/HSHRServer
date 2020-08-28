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
    using std::once_flag;

    template <typename Restype>
    class threadpool {
      private:
        const int MAX_THREAD_NUM;
        const int MAX_QUEUE_NUM;
        mutex mtx;
        bool isrun;
        once_flag flag;
        condition_variable notEmpty;
        condition_variable notFull;
        queue<Restype> resQueue;
        vector<unique_ptr<thread>> threadGroup;

      public:
        threadpool(int threadnum, int queuenum)
            : isrun(true),MAX_THREAD_NUM(threadnum), MAX_QUEUE_NUM(queuenum), threadGroup(MAX_THREAD_NUM) {}
        ~threadpool(){
            stop();
        }
        threadpool(const threadpool&) = delete;
        threadpool& operator=(const threadpool&) = delete;

        bool isFull() { return resQueue.size() == MAX_QUEUE_NUM; }

        bool isEmpty() { return resQueue.empty(); }

        void stop(){
            std::call_once(flag,[this](){
                {
                    lock_guard<mutex> locker(mtx);
                    isrun = false;
                }
                notEmpty.notify_all();
                notFull.notify_all();
                for(int i = 0; i <MAX_THREAD_NUM ;++i){
                    threadGroup[i]->join();
                }
            });
        }

        void add(Restype res) {
            unique_lock<mutex> locker(mtx);
            notFull.wait(locker, [this] { return !isFull() || !isrun; });
            if (!isrun)
                return ;
            resQueue.push(res);
            notEmpty.notify_one();
        }

        Restype take() {
            unique_lock<mutex> locker(mtx);
            notEmpty.wait(locker, [this] { return !isEmpty() || !isrun; });
            if (!isrun)
                return nullptr;
            Restype res = resQueue.front();
            resQueue.pop();
            notFull.notify_one();
            return res;
        }

        template <typename Processtype>
        void task() {
            while (isrun) {
                Restype res = take();
                if (res == nullptr){
                    continue;
                }
                Processtype(res).process();
            }
        }

        template <typename Processtype>
        void work() {
            for (int i = 0; i < MAX_THREAD_NUM; ++i) {
                threadGroup[i]
                    = std::make_unique<thread>((&threadpool<Restype>::task<Processtype>), this);
            }
        }
    };

}  // namespace sinksky