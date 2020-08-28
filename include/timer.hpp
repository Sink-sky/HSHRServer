#pragma once
#include <time.h>

#include <memory>
#include <queue>

namespace sinksky {
    using std::priority_queue;
    using std::unique_ptr;
    using std::vector;

    // 计时器
    // 不可复制不可移动
    // 定义泛型方便日后需要回调函数返回值的时候
    // 使用闭包来给回调函数必要的回调信息
    template <typename Functype>
    class timerNode {
      private:
        bool vaildFlag;
        const time_t expire;
        Functype callBack;

      public:
        timerNode(int delay, Functype callback)
            : vaildFlag(true), expire(time(NULL) + delay), callBack(callback) {}
        ~timerNode() = default;
        timerNode(const timerNode&) = delete;
        timerNode& operator=(const timerNode&) = delete;

        bool getVaild() { return vaildFlag; }
        //只有确认不再使用才能将Flag置为False
        void setVaild() { vaildFlag = false; }
        time_t getExpire() { return expire; }
        Functype getCallBack() { return callBack; }
        decltype(auto) callFunc() { return callBack(); }
    };

    // 计时器的所有权归计时器管理器
    template <typename Functype>
    using UPTimeNode = unique_ptr<timerNode<Functype>>;

    // 优先队列排列顺序的仿函数
    template <typename Functype>
    struct cmpTimerNode {
        bool operator()(const UPTimeNode<Functype>& tn1, const UPTimeNode<Functype>& tn2) {
            return tn1->getExpire() > tn2->getExpire();
        }
    };

    // 计时器管理器
    // 不可复制不可移动
    // 使用优先队列实现
    template <typename Funtype>
    class timerHeap {
      private:
        priority_queue<UPTimeNode<Funtype>, vector<UPTimeNode<Funtype>>, cmpTimerNode<Funtype>>
            heap;

      public:
        timerHeap() = default;
        ~timerHeap() = default;
        timerHeap(const timerHeap&) = delete;
        timerHeap& operator=(const timerHeap&) = delete;

        // 完美转发计时器构造函数
        // 返回一个原始指针作为操作索引
        template <typename... Paramtype>
        timerNode<Funtype>* addTimer(Paramtype&&... param) {
            auto ptr = std::make_unique<timerNode<Funtype>>(std::forward<Paramtype>(param)...);
            auto ret = ptr.get();
            heap.push(std::move(ptr));
            return ret;
        }

        void delTimer(timerNode<Funtype>* timer) { timer->setVaild(); }

        timerNode<Funtype>* updateTimer(timerNode<Funtype>* timer, time_t delay) {
            auto ret = addTimer(delay, timer->getCallBack());
            delTimer(timer);
            return ret;
        }

      private:
        timerNode<Funtype>* topTimer() {
            if (isEmpty())
                return nullptr;
            else
                return heap.top().get();
        }
        void rmTopTimer() { heap.pop(); }
        bool isEmpty() { return heap.empty(); }

      public:
        void tick() {
            timerNode<Funtype>* timer = topTimer();
            time_t cur = time(NULL);
            while (!isEmpty()) {
                if (timer == nullptr) {
                    break;
                } else if (timer->getVaild() == false) {
                    rmTopTimer();
                    timer = topTimer();
                } else if (timer->getExpire() > cur) {
                    break;
                } else {
                    timer->callFunc();
                    rmTopTimer();
                    timer = topTimer();
                }
            }
        }
    };

}  // namespace sinksky