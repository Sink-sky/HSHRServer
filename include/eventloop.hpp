#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <memory>

#include "timer.hpp"

namespace sinksky {
    using std::function;
    using std::unique_ptr;

    using timerNodev = timerNode<function<void()>>;
    using timerHeapv = timerHeap<function<void()>>;

    template <typename Datatype>
    class eventloop;

    template <typename Datatype>
    struct conn {
        int fd;
        decltype(epoll_event::events) statu;
        timerNodev *timer;
        eventloop<Datatype> *op;
        unique_ptr<Datatype> data;
    };

    // 连接,监听,定时,统一事件源
    template <typename Datatype>
    class eventloop {
      public:
        static const int MAX_EVENT_NUM = 4096;
        static const int TIMESLOT = 5;

      private:
        static const int MAX_CONN_FD = 100000;
        int epfd;
        int listenfd;
        static int pipefd[2];
        bool isTimeout;
        unique_ptr<epoll_event[]> events;
        timerHeapv timerManage;
        unique_ptr<conn<Datatype>> fd2conn[MAX_CONN_FD];

      private:
        void setNonBlocking(int fd) {
            int oldopt = fcntl(fd, F_GETFL);
            int newopt = oldopt | O_NONBLOCK;
            fcntl(fd, F_SETFL, newopt);
        }

        void addfd(int fd, bool oneshot = false) {
            epoll_event event;
            event.data.fd = fd;
            event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            if (oneshot) event.events |= EPOLLONESHOT;
            epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
            setNonBlocking(fd);
        }

        void removefd(int fd) {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
        }
        void modfd(int fd, int ev) {
            epoll_event event;
            event.data.fd = fd;
            event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
            epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
        }
        static void sigHandler(int sig) {
            int save_errno = errno;
            int msg = sig;
            send(pipefd[1], (char *)&msg, 1, 0);
            errno = save_errno;
        }

        void addSig(int sig) {
            struct sigaction sa;
            bzero(&sa, sizeof(sa));
            sa.sa_handler = sigHandler;
            sa.sa_flags |= SA_RESTART;
            sigfillset(&sa.sa_mask);
            sigaction(sig, &sa, NULL);
        }

        void timerHandle() {
            timerManage.tick();
            alarm(TIMESLOT);
        }

        void initPipe() {
            socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
            setNonBlocking(pipefd[1]);
            addfd(pipefd[0]);
            addSig(SIGALRM);
            addSig(SIGTERM);
        }

        void initListen(const char *ip, int port) {
            sockaddr_in address;
            bzero(&address, sizeof(address));
            address.sin_family = AF_INET;
            inet_pton(AF_INET, ip, &address.sin_addr);
            address.sin_port = htons(port);

            listenfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
            int optval = 1;
            setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

            bind(listenfd, (sockaddr *)&address, sizeof(address));
            listen(listenfd, MAX_EVENT_NUM);

            addfd(listenfd);
        }

      public:
        eventloop()
            : epfd(epoll_create(MAX_EVENT_NUM)),
              isTimeout(false),
              events(std::make_unique<epoll_event[]>(MAX_EVENT_NUM)) {}

        ~eventloop() {
            close(epfd);
            close(listenfd);
            close(pipefd[0]);
            close(pipefd[1]);
        }

        eventloop(const eventloop &) = delete;

        eventloop &operator=(const eventloop &) = delete;

        void delConnfd(int fd) {
            timerManage.delTimer(fd2conn[fd]->timer);
            fd2conn[fd].reset();
            removefd(fd);
        }

        void addConnfd(int fd) {
            addfd(fd, true);
            fd2conn[fd] = std::make_unique<conn<Datatype>>();
            fd2conn[fd]->fd = fd;
            fd2conn[fd]->op = this;
            fd2conn[fd]->data = std::make_unique<Datatype>();
            timerNodev *ptr
                = timerManage.addTimer(3 * TIMESLOT, [this, fd]() -> void { delConnfd(fd); });
            fd2conn[fd]->timer = ptr;
        }

        void modConnfd(int fd, int ev) { modfd(fd, ev); }

        template <typename Threadpooltype>
        void loop(const char *ip, int port, Threadpooltype *pool) {
            initListen(ip, port);
            initPipe();
            alarm(TIMESLOT);
            bool runLoop = true;
            while (runLoop) {
                int cnt = epoll_wait(epfd, events.get(), MAX_EVENT_NUM, -1);

                for (int i = 0; i < cnt; ++i) {
                    if (events[i].data.fd == listenfd) {
                        int connfd;
                        sockaddr_in address;
                        socklen_t len;
                        while ((connfd = accept(listenfd, (sockaddr *)&address, &len)) > 0) {
                            addConnfd(connfd);
                        }
                    } else if (events[i].data.fd == pipefd[0]) {
                        char signals[1024];
                        int num = recv(pipefd[0], signals, sizeof(signals), 0);
                        if (num == -1)
                            continue;
                        else if (num == 0)
                            continue;
                        else {
                            for (int j = 0; j < num; j++) {
                                switch (signals[i]) {
                                    case SIGALRM: {
                                        isTimeout = true;
                                        break;
                                    }
                                    case SIGTERM: {
                                        runLoop = false;
                                    }
                                }
                            }
                        }
                    } else if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
                        delConnfd(events[i].data.fd);
                    } else {
                        int alreadyfd = events[i].data.fd;
                        fd2conn[alreadyfd]->statu = events[i].events;
                        auto newtimer
                            = timerManage.updateTimer(fd2conn[alreadyfd]->timer, 3 * TIMESLOT);
                        fd2conn[alreadyfd]->timer = newtimer;
                        pool->add(fd2conn[alreadyfd].get());
                    }
                }
                if (isTimeout) {
                    timerHandle();
                    isTimeout = false;
                }
            }
        }
    };

    template <typename Datatype>
    int eventloop<Datatype>::pipefd[2] = {-1, -1};

}  // namespace sinksky