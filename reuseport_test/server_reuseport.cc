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
#include<fstream>
#define PORT 9527
#define MAX_EVENT 65535
using namespace std;

const string prefix = "thread_";
uint32_t default_event = EPOLLIN | EPOLLEXCLUSIVE;

class IgnoreSigPipe
{
public:
    IgnoreSigPipe()
    {   
        ::signal(SIGPIPE, SIG_IGN);
    }
};


struct Conn{
    uint32_t Conn_event;
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

IgnoreSigPipe initObj;
int main(int argc, char* argv[])
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(argc < 2){
        cout<<"请填写进程数"<<endl;
        exit(1);
    }
    string fork_s(argv[1]);
    int fork_n;
    {
        stringstream ss(fork_s);
        ss >> fork_n;
    }


    auto do_work = [&](int fork_index){
        uint64_t counter = 0;
        string file_name = prefix + to_string(fork_index);
        ofstream out(file_name, ios::out);
        assert(out.is_open());

        int listenFd = socket(AF_INET, SOCK_STREAM, 0);
        assert(listenFd >= 0);

        setReuseAddr(listenFd, 1);
        setReusePort(listenFd, 1);
        
        int ret = bind(listenFd, (struct sockaddr* )&server_addr, sizeof server_addr);
        assert(ret != -1);

        ret = listen(listenFd, 256);
        assert(ret != -1);
        setNonblock(listenFd);

        int epollFd = epoll_create(512);
        assert(epollFd >= 0);

        struct epoll_event events_[MAX_EVENT];

        struct Conn* newConn_L = new struct Conn;
        newConn_L->Conn_addr = {0};
        newConn_L->Conn_addr_len = 0;
        newConn_L->Conn_event = default_event;
        newConn_L->Conn_fd = listenFd;

        struct epoll_event ev = {0};
        ev.data.ptr = (void *)newConn_L;
        ev.events = default_event;

        if( epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &ev) != 0)  {
            perror("epoll_ctl");
            exit(1);
        }

        while(1) 
        {
            //cout<<"i am worker no."<<i+1<<" , begin to accept connection"<<endl;
            int n = epoll_wait(epollFd, events_, MAX_EVENT, -1);
            for(int i = 0 ; i < n; ++i)
            {   
                auto ptr = (struct Conn*)events_[i].data.ptr;
                auto fd = ptr->Conn_fd;
                auto eve = events_[i].events;
                if(fd == listenFd)
                {
                    struct sockaddr_in client_addr;
                    socklen_t client_addr_len;
                    auto newConnectionFd = accept(listenFd, (struct sockaddr* )&client_addr, &client_addr_len);
                    if(newConnectionFd < 0)continue;

                    struct Conn* newConn = new struct Conn;
                    newConn->Conn_addr = client_addr;
                    newConn->Conn_addr_len = client_addr_len;
                    newConn->Conn_event = default_event;
                    newConn->Conn_fd = newConnectionFd;
                    struct epoll_event ev = {0};
                    ev.data.ptr = (void*)newConn;
                    ev.events = default_event;

                    setNonblock(newConnectionFd);
                    setTcpNoDelay(newConnectionFd, 1);
                    if( epoll_ctl(epollFd, EPOLL_CTL_ADD, newConnectionFd, &ev) != 0)  {
                        perror("epoll_ctl");
                        exit(1);
                    }
                }
                else
                {
                    if(eve & EPOLLIN) 
                    {
                        char buf[BUFSIZ];
                        int readn = read(fd, buf, BUFSIZ);
                        if( readn == 0 || (readn < 0 && errno != EAGAIN) ) {
                            if( epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) != 0)  {
                                perror("epoll_ctl");
                                exit(1);
                            }
                            delete ptr;
                            close(fd);
                        }
                        else if(readn > 0) {
                            ++counter;
                            out << counter << "\t";
                            for(int i = 0; i < readn; ++i) {
                                out<<buf[i];
                            }
                            out<<endl;
                        }

                    }
                }
            }
        }
        close(epollFd);
        close(listenFd);

    };

    int i = 0;
    for(; i < fork_n; ++i) {
        cout<<"creating no. "<<i+1<<"worker"<<endl;
        auto pid = fork();
        if(pid == 0){
            do_work(i);
        }
        else if(pid < 0){
            perror("fork");
            exit(1);
        }
    }

    while( (waitpid(-1, nullptr, 0)) != -1);
    
    return 0;

}