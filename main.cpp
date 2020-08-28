#include <eventloop.hpp>
#include <thread>
#include <threadpool.hpp>

#include "http/httpdata.cpp"
#include "http/httpprocess.cpp"

int main(int argc, char* argv[]) {
    using sinksky::conn;
    using sinksky::eventloop;
    using sinksky::httpdata;
    using sinksky::httpprocess;
    using sinksky::threadpool;

    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    eventloop<httpdata> loop;
    threadpool<conn<httpdata>*> pool(std::thread::hardware_concurrency(),
                                     eventloop<httpdata>::MAX_EVENT_NUM);
    pool.work<httpprocess>();
    loop.loop(ip, port, &pool);
    pool.stop();
    return 0;
}