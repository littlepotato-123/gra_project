#include <unistd.h>
#include <iostream>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <errno.h>
#include<thread>
#include <stdio.h>
#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include<signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include<memory>
#include<cstring>
#include<sstream>
#include<mutex>
#include<condition_variable>
#include<fstream>
#define OUTER_PORT 9527
#define INNER_PORT 9528
#define MON_PORT 9529
#define MAX_EVENT 65535
#define SERVERS_NUM 4
using namespace std;


using pii = pair<int,int>;


uint32_t default_event = EPOLLIN ;

class IgnoreSigPipe
{
public:
    IgnoreSigPipe()
    {   
        ::signal(SIGPIPE, SIG_IGN);
    }
};


struct Conn{
   // uint32_t Conn_event;
    int Conn_fd;
    socklen_t Conn_addr_len;
    sockaddr_in Conn_addr;
};



void setNonblock(int socketFd) 
{   
    fcntl(socketFd, F_SETFL, fcntl(socketFd, F_GETFD, 0) | O_NONBLOCK);
}

void setTcpNoDelay(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    
    if(::setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof optval) < 0){
        perror("setsockopt  reuseport:");
        exit(1);
    }
}

void setReusePort(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    if(::setsockopt(socketFd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof optval) < 0){
        perror("setsockopt  reuseport:");
        exit(1);
    }
}

void setReuseAddr(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    if(::setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) < 0){
        perror("setsockopt  reuseport:");
        exit(1);
    }
}

int socket_connect_mon(const struct sockaddr_in& client_addr_inner){
    auto cfd = socket(AF_INET, SOCK_STREAM, 0);
        if(cfd < 0){
            perror("mon socket");
            exit(1);
        }
        struct sockaddr_in tmp_addr;
        tmp_addr.sin_family = AF_INET;
        tmp_addr.sin_port = htons(MON_PORT);
        tmp_addr.sin_addr.s_addr = client_addr_inner.sin_addr.s_addr;
        int ret = connect(cfd, (struct sockaddr* )&tmp_addr, sizeof(tmp_addr));
        if(ret != 0) {
            perror("mon connnect");
            exit(1);
        }
        return cfd;
}

int GetListenFd(int port, int backlog) {
    struct sockaddr_in server_addr_inner;
    memset(&server_addr_inner, 0, sizeof server_addr_inner);
    server_addr_inner.sin_family = AF_INET;
    server_addr_inner.sin_port = htons(port);
    server_addr_inner.sin_addr.s_addr = htonl(INADDR_ANY);

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenFd >= 0);

    setReuseAddr(listenFd, 1);

    int ret = bind(listenFd, (struct sockaddr* )&server_addr_inner, sizeof server_addr_inner);
    assert(ret != -1);

    ret = listen(listenFd, backlog);
    assert(ret != -1);
    return listenFd;
}

void EpollCtl_Add(int epollFd, int fd, uint32_t _event_) {
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = _event_;
    if( epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) != 0)  {
        perror("epoll_ctl add");
        exit(1);
    }
}

void CloseConnect(int epollFd, int fd) {
    if( epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) != 0)  {
        perror("epoll_ctl del");
        exit(1);
    }
    close(fd);
}

IgnoreSigPipe initObj;
pii servers_inner[SERVERS_NUM];
struct epoll_event events_[MAX_EVENT];
int CurSelectedServerIndex = 0;
condition_variable cond;
mutex mtx;
int main(int argc, char* argv[])
{
    //memset(&servers_inner, 0, sizeof servers_inner);

    int listenFd_inner = GetListenFd(INNER_PORT, SERVERS_NUM);

    for(int i = 0; i < SERVERS_NUM; ++i) {
        struct sockaddr_in client_addr_inner;
        socklen_t client_addr_inner_len;
        auto newConnectionFd = accept(listenFd_inner, (struct sockaddr* )&client_addr_inner, &client_addr_inner_len);
        if(newConnectionFd < 0) {
            perror("inner connnect!");
            exit(1);
        }
        int mon_fd = socket_connect_mon(client_addr_inner);
        setTcpNoDelay(mon_fd, 1);
        setTcpNoDelay(newConnectionFd, 1);
        servers_inner[i] = {newConnectionFd, mon_fd};
    }

    auto threadFunc = []() {
        char buf[BUFSIZ];
        const char correct_res[16] = {'a'};
        const char err_res[16] = {'b'};
        int readn;
        int writen;
        int listenFd_outer = GetListenFd(OUTER_PORT, 512);
        setNonblock(listenFd_outer);

        int epollFd = epoll_create(1024);
        assert(epollFd >= 0);
        
        EpollCtl_Add(epollFd, listenFd_outer, default_event);

        while(1) {

            int n = epoll_wait(epollFd, events_, MAX_EVENT, -1);

            for(int i = 0; i < n; ++i) {
                if(events_[i].data.fd == listenFd_outer) {
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len;
                    while(1) 
                    {
                        auto newConnectionFd = accept(listenFd_outer, (struct sockaddr* )&client_addr, &client_addr_len);
                        if(newConnectionFd < 0 ) {
                            if(errno == EAGAIN) break;
                            else {
                                perror("accept outer");
                                exit(1);
                            }
                        }
                        setNonblock(newConnectionFd);
                        setTcpNoDelay(newConnectionFd, 1);
                        EpollCtl_Add(epollFd, newConnectionFd, default_event);
                    } 
                }
                else if(events_[i].events & EPOLLIN) 
                {
                    auto fd = events_[i].data.fd;
                    readn = read(fd , buf, BUFSIZ);
                    if( readn == 0 || (readn < 0 && errno != EAGAIN)) CloseConnect(epollFd, fd);
                    else if(readn > 0) 
                    {
                        int CurSelectedServer_fd;
                        {
                            unique_lock<mutex> locker(mtx);
                            while(CurSelectedServerIndex < 0) {
                                cond.wait(locker);
                            }
                            CurSelectedServer_fd = servers_inner[CurSelectedServerIndex].first;
                            if(++CurSelectedServerIndex >= SERVERS_NUM) CurSelectedServerIndex = 0;
                        }
                        
                        writen = write(CurSelectedServer_fd, buf, readn);
                        if(writen == readn) 
                            write(fd, correct_res, 16);
                        else 
                            write(fd, err_res, 16);
                    }
                }   
            }
        }
        close(epollFd);
        close(listenFd_outer);
    };

    thread(threadFunc).detach();
    return 0;
}