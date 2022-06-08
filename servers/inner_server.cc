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
#include<unordered_set>
#include<mutex>
#include<condition_variable>
#include<fstream>
#include<random>
#include "pm.h"
#define INNER_PORT 9528
#define MON_PORT 9529
#define MAX_EVENT 65535
#define DISTRIBUTOR_IP "127.0.0.1"
using namespace std;

using pii = pair<int,int>;

struct message {
    int fd;
    char m[16];
};
class IgnoreSigPipe
{
public:
    IgnoreSigPipe()
    {   
        ::signal(SIGPIPE, SIG_IGN);
    }
};

IgnoreSigPipe initObj;
const int messLen = 20;
const int normalLen = 16;
uint32_t default_event = EPOLLIN ;
struct epoll_event events_[MAX_EVENT];
condition_variable cond;
mutex mtx;
char rec_for_mon ;
char send_for_mon[24];



void do_something() 
{
    for(int i = 100; i < 150; ++i) 
    {
        sqrt(i);
    }
    char tmpBuf[100] = "a";
    int ioFd = open("do_something", O_RDWR | O_CREAT, 0755);
    write(ioFd, tmpBuf, 100);
    close(ioFd);
}
void setNonblock(int socketFd) 
{   
    fcntl(socketFd, F_SETFL, fcntl(socketFd, F_GETFD, 0) | O_NONBLOCK);
}

void setTcpNoDelay(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    
    if(::setsockopt(socketFd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof optval) < 0)
    {
        perror("setsockopt  reuseport:");
        exit(1);
    }
}

void setReuseAddr(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    if(::setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) < 0)
    {
        perror("setsockopt  reuseport:");
        exit(1);
    }
}

int GetListenFd(int port, int backlog)
{
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

int socket_connect_mon ()
{
    auto cfd = socket(AF_INET, SOCK_STREAM, 0);
        if(cfd < 0)
        {
            perror("mon socket");
            exit(1);
        }
        struct sockaddr_in tmp_addr;
        tmp_addr.sin_family = AF_INET;
        tmp_addr.sin_port = htons(INNER_PORT);
        inet_pton(AF_INET, DISTRIBUTOR_IP, &tmp_addr.sin_addr);
        int ret = connect(cfd, (struct sockaddr* )&tmp_addr, sizeof(tmp_addr));
        if(ret != 0) 
        {
            perror("mon connnect");
            exit(1);
        }
        return cfd;
}

//inner 
void threadFunc () 
{
    int do_work_fd = socket_connect_mon();
    setTcpNoDelay(do_work_fd, 1);
    int readn ;
    message mess;
    while(1)
    {   
        readn = read(do_work_fd, &mess, messLen);
        if(readn < 0) 
        {
            perror("do_work_fd read!");
            exit(1);
        }
        else if(readn == 0)
        {
            perror("distributor closed");
            close(do_work_fd);
            exit(1);
        }
        else if(readn == messLen)
        {
            int markFd = mess.fd;
            do_something();
            write(do_work_fd, &mess, messLen);
        }
    }  
}
//mon
void mainThreadFunc() 
{
    int monLfd = GetListenFd(MON_PORT, 1);
    struct sockaddr_in distributor_addr;
    socklen_t distributor_addr_len;
    int monFd = accept(monLfd, (struct sockaddr* )&distributor_addr, &distributor_addr_len);
    if(monFd < 0) 
    {
        perror("mon connnect!");
        exit(1);
    }
    setTcpNoDelay(monFd, 1);
    int readn;
    int writen;
    Procmon pm;
    while(1) 
    {
        readn = read(monFd, &rec_for_mon, 1);
        if(readn <= 0)
        {
            perror("monFd read!");
            exit(1);
        }
        else 
        {
            auto usage = pm.getStat();
            double* cur = (double*) send_for_mon;
            *cur = get<0>(usage);
            *(cur+1) = get<1>(usage);
            *(cur+2) = get<2>(usage);
            writen = write(monFd, send_for_mon, 3*sizeof(double));
            if(writen !=  3*sizeof(double))
            {
                perror("monFd writen <> 24");
                exit(1);
            }
        }
    }
}
int main(int argc, char* argv[])
{
    
    thread(threadFunc).detach();
    mainThreadFunc();
    return 0;
}