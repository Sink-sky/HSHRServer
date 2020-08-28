#pragma once
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <memory>

namespace sinksky {
    enum class Method { GET, POST };
    enum class HttpCode {
        NO_REQUEST,
        BAD_REQUEST,
        GET_REQUEST,
        INTERNAL_ERROR,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        NO_RESOURCE
    };
    enum class CheckState { CHECK_REQUESTLINE, CHECK_HEADER, CHECK_CONTENT };
    enum class LineState { LINE_OK, LINE_BAD, LINE_OPEN };

    using std::string;
    using std::unique_ptr;

    class httpprocess;

    class httpdata {
        friend class httpprocess;

      public:
        static const int READ_BUF_SIZE = 2048;
        static const int WRITE_BUF_SIZE = 2048;
        static const string root;

      private:
        Method method;

        unique_ptr<char[]> readBuf;
        int readIdx;
        unique_ptr<char[]> writeBuf;
        int writeIdx;
        int haveWriteIdx;
        char *fileAddress;
        struct stat fileStat;
        iovec writeIv[2];
        int writeIvCount;

        bool linger;
        string url;
        CheckState checkState;

      public:
        httpdata()
            : method(Method::GET),
              readBuf(std::make_unique<char[]>(READ_BUF_SIZE)),
              readIdx(0),
              writeBuf(std::make_unique<char[]>(WRITE_BUF_SIZE)),
              writeIdx(0),
              haveWriteIdx(0),
              fileAddress(nullptr),
              linger(true),
              checkState(CheckState::CHECK_REQUESTLINE),
              writeIvCount(0) {}
        ~httpdata() {
            if (fileAddress != nullptr) {
                munmap(fileAddress, fileStat.st_size);
            }
        }
        httpdata(const httpdata &) = delete;
        httpdata &operator=(const httpdata &) = delete;

        void map(void *addr, size_t len, int prot, int flags, int fd, __off_t offset) {
            fileAddress = (char *)mmap(addr, len, prot, flags, fd, offset);
        }

        void unmap() {
            if (fileAddress != nullptr) {
                munmap(fileAddress, fileStat.st_size);
                fileAddress = nullptr;
            }
        }

        void init() {
            method = Method::GET;
            readIdx = 0;
            writeIdx = 0;
            haveWriteIdx = 0;
            unmap();
            linger = true;
            checkState = CheckState::CHECK_REQUESTLINE;
            writeIvCount = 0;
            memset(readBuf.get(), '\0', READ_BUF_SIZE);
            memset(writeBuf.get(), '\0', WRITE_BUF_SIZE);
        }
    };

    const string httpdata::root("/var/www/html");
}  // namespace sinksky