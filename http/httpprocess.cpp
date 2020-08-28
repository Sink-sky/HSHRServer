#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <eventloop.hpp>
#include <regex>

#include "httpdata.cpp"

namespace sinksky {
    using std::cmatch;
    using std::regex;
    using std::smatch;
    using std::string;

    const char *ok_200_title = "OK";
    const char *error_400_title = "Bad Request";
    const char *error_400_form
        = "Your request has bad syntax or is inherently impossible to satisfy.\n";
    const char *error_403_title = "Forbidden";
    const char *error_403_form = "You do not have permission to get file from this server.\n";
    const char *error_404_title = "Not Found";
    const char *error_404_form = "The requested file was not found on this server.\n";
    const char *error_500_title = "Internal Error";
    const char *error_500_form = "There was an unusual problem serving the requested file.\n";

    class httpprocess {
      private:
        conn<httpdata> const *conndata;

        LineState parseLine(const char *&start, cmatch *match) {
            httpdata *data = conndata->data.get();
            if (std::regex_search(start, *match, regex("^[\\s\\S]*?\r\n"))) {
                start = (*match)[0].second;
                return LineState::LINE_OK;
            }
            //            else if(std::regex_search(*start, *match, regex("^[\\s\\S]*\\r$"))){
            //                return LineState::LINE_OPEN;
            //            }
            else {
                return LineState::LINE_BAD;
            }
        }

        HttpCode parseRequestLine(const char *str) {
            httpdata *data = conndata->data.get();
            cmatch match;
            if (!std::regex_search(str, match, regex("^[\\S]*?\\s"))) {
                return HttpCode::BAD_REQUEST;
            }
            str = match[0].second;
            if (!match[0].str().compare(0, sizeof("GET") - 1, "GET")) {
                data->method = Method::GET;
            } else {
                return HttpCode::BAD_REQUEST;
            }

            if (!std::regex_search(str, match, regex("^[\\S]*?\\s"))) {
                return HttpCode::BAD_REQUEST;
            }
            str = match[0].second;
            data->url = match[0].str().substr(0, match[0].length() - 1);

            if (data->url.substr(0, 1) != "/") return HttpCode::BAD_REQUEST;

            if (!std::regex_search(str, match, regex("^[\\S]*?\\r\\n"))) {
                return HttpCode::BAD_REQUEST;
            }
            if (match[0].str().compare(0, sizeof("HTTP/1.1") - 1, "HTTP/1.1")) {
                return HttpCode::BAD_REQUEST;
            }

            data->checkState = CheckState::CHECK_HEADER;
            return HttpCode::NO_REQUEST;
        }

        HttpCode parseHeader(const char *str) {
            httpdata *data = conndata->data.get();
            if (str[0] == '\r' && str[1] == '\n') {
                data->checkState = CheckState::CHECK_CONTENT;
                return HttpCode::GET_REQUEST;
            } else if (std::regex_search(str, regex("^Connection:"))) {
                if (!std::regex_search(str, regex("keep-alive\\r\\n$",std::regex::icase))) {
                    data->linger = data->linger && false;
                }
            } else {
                // TODO 其他字段
            }
            return HttpCode::NO_REQUEST;
        }
        //        HttpCode parseContent(){
        //            // TODO 未解析请求体
        //            return HttpCode::GET_REQUEST;
        //        }

        HttpCode doRequest() {
            httpdata *data = conndata->data.get();
            string filepath = httpdata::root + data->url;
            if (stat(filepath.c_str(), &(data->fileStat)) < 0) {
                return HttpCode::NO_RESOURCE;
            }
            if (!(data->fileStat.st_mode & S_IROTH)) {
                return HttpCode::FORBIDDEN_REQUEST;
            }
            if (S_ISDIR(data->fileStat.st_mode)) {
                return HttpCode::BAD_REQUEST;
            }
            int fd = open(filepath.c_str(), O_RDONLY);
            data->map(0, data->fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            close(fd);
            return HttpCode::FILE_REQUEST;
        }

        HttpCode processRead() {
            httpdata *data = conndata->data.get();
            LineState lineState = LineState::LINE_OK;
            HttpCode ret = HttpCode::NO_REQUEST;
            cmatch match;
            const char *text = data->readBuf.get();
            while ((lineState = parseLine(text, &match)) == LineState::LINE_OK) {
                switch (data->checkState) {
                    case CheckState::CHECK_REQUESTLINE: {
                        ret = parseRequestLine(match[0].str().c_str());
                        if (ret == HttpCode::BAD_REQUEST) {
                            return HttpCode::BAD_REQUEST;
                        }
                        break;
                    }
                    case CheckState::CHECK_HEADER: {
                        ret = parseHeader(match[0].str().c_str());
                        if (ret == HttpCode::BAD_REQUEST) {
                            return HttpCode::BAD_REQUEST;
                        } else if (ret == HttpCode::GET_REQUEST) {
                            return doRequest();
                        }
                        break;
                    }
                        //                    case CheckState::CHECK_CONTENT: {
                        //                        ret = parseContent();
                        //                        if (ret == HttpCode::GET_REQUEST) {
                        //                            return doRequest();
                        //                        }
                        //                        break;
                        //                    }
                    default: {
                        return HttpCode::INTERNAL_ERROR;
                    }
                }
            }
            return HttpCode::BAD_REQUEST;
        }

        template <typename... Paramtype>
        void addResponse(Paramtype &&... param) {
            httpdata *data = conndata->data.get();
            if (data->writeIdx >= data->WRITE_BUF_SIZE) {
                return;
            }
            int len = snprintf(data->writeBuf.get() + data->writeIdx,
                               data->WRITE_BUF_SIZE - data->writeIdx - 1,
                               std::forward<Paramtype>(param)...);
            data->writeIdx += len;
        }

        void addStatusLine(int status, const char *title) {
            addResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
        }

        void addContentLength(int len) { addResponse("Content-Length: %d\r\n", len); }

        void addLinger() {
            addResponse("Connection: %s\r\n",
                        (conndata->data->linger == true) ? "keep-alive" : "close");
        }

        void addBlackLine() { addResponse("%s", "\r\n"); }

        void addHearders(int len) {
            addContentLength(len);
            addLinger();
            addBlackLine();
        }

        void addContent(const char *content) { addResponse("%s", content); }

        void processWrite(HttpCode ret) {
            httpdata *data = conndata->data.get();
            switch (ret) {
                case HttpCode::INTERNAL_ERROR: {
                    addStatusLine(500, error_500_title);
                    addHearders(strlen(error_500_form));
                    addContent(error_500_form);
                    break;
                }
                case HttpCode::BAD_REQUEST: {
                    addStatusLine(400, error_400_title);
                    addHearders(strlen(error_400_form));
                    addContent(error_400_form);
                    break;
                }
                case HttpCode::NO_RESOURCE: {
                    addStatusLine(404, error_404_title);
                    addHearders(strlen(error_404_form));
                    addContent(error_404_form);
                    break;
                }
                case HttpCode::FORBIDDEN_REQUEST: {
                    addStatusLine(403, error_403_title);
                    addHearders(strlen(error_403_form));
                    addContent(error_403_form);
                    break;
                }
                case HttpCode::FILE_REQUEST: {
                    addStatusLine(200, ok_200_title);
                    if (data->fileStat.st_size != 0) {
                        addHearders(data->fileStat.st_size);
                        data->writeIv[0].iov_base = data->writeBuf.get();
                        data->writeIv[0].iov_len = data->writeIdx;
                        data->writeIv[1].iov_base = data->fileAddress;
                        data->writeIv[1].iov_len = data->fileStat.st_size;
                        data->writeIvCount = 2;
                        return;
                    } else {
                        const char *okstr = "<html><body></body></html>";
                        addHearders(strlen(okstr));
                        addContent(okstr);
                    }
                }
            }
            data->writeIv[0].iov_base = data->writeBuf.get();
            data->writeIv[0].iov_len = data->writeIdx;
            data->writeIvCount = 1;
        }

        bool readBuf() {
            httpdata *data = conndata->data.get();
            if (data->readIdx >= httpdata::READ_BUF_SIZE) {
                return false;
            }

            while (true) {
                auto cnt = recv(conndata->fd, data->readBuf.get() + data->readIdx,
                                httpdata::READ_BUF_SIZE - data->readIdx, 0);
                if (cnt == -1) {
                    if (errno == EAGAIN) break;
                    return false;
                } else if (cnt == 0) {
                    shutdown(conndata->fd, SHUT_RD);
                    data->linger = data->linger && false;
                    break;
                } else {
                    data->readIdx += cnt;
                }
            }
            return true;
        }

        void adjustIov(iovec iov[], int &iovcnt) {
            httpdata *data = conndata->data.get();
            iovcnt = 0;
            int done = data->haveWriteIdx;
            for (int i = 0; i < data->writeIvCount; ++i) {
                if (done >= data->writeIv[i].iov_len) {
                    done -= data->writeIv[i].iov_len;
                } else {
                    iov[iovcnt].iov_base = (char *)data->writeIv[i].iov_base + done;
                    iov[iovcnt].iov_len = data->writeIv[i].iov_len - done;
                    ++iovcnt;
                    done = 0;
                }
            }
        }

        void writeBuf() {
            // TODO 修改haveWriteIdx
            httpdata *data = conndata->data.get();
            int sum = 0;
            for (int i = 0; i < data->writeIvCount; ++i) sum += data->writeIv[i].iov_len;
            while (true) {
                iovec iov[2];
                int iovcnt;
                adjustIov(iov, iovcnt);
                auto cnt = writev(conndata->fd, iov, iovcnt);
                if (cnt == -1) {
                    if (errno == EAGAIN) {
                        conndata->op->modConnfd(conndata->fd, EPOLLOUT);
                        return;
                    }
                }
                data->haveWriteIdx += cnt;
                if (data->haveWriteIdx >= sum) {
                    data->unmap();
                    if (data->linger) {
                        data->init();
                        conndata->op->modConnfd(conndata->fd, EPOLLIN);
                    } else {
                        conndata->op->delConnfd(conndata->fd);
                    }
                    return;
                }
            }
        }

      public:
        httpprocess(conn<httpdata> *conndata) : conndata(conndata) {}
        ~httpprocess() = default;
        httpprocess(const httpprocess &) = delete;
        httpprocess &operator=(const httpprocess &) = delete;

        void process() {
            if (conndata->statu & EPOLLIN) {
                bool ret = readBuf();
                if (!ret) {
                    conndata->op->delConnfd(conndata->fd);
                    return;
                } else {
                    processWrite(processRead());
                    writeBuf();
                }
            } else if (conndata->statu & EPOLLOUT) {
                writeBuf();
            }
        }
    };

}  // namespace sinksky