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
#define OUTER_PORT 9527
#define INNER_PORT 9528
#define MON_PORT 9529
#define MAX_EVENT 65535
#define SERVERS_NUM 4
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


const int messLen = 20;
const int normalLen = 16;
vector<double> p(SERVERS_NUM, 100.0/SERVERS_NUM);
unordered_set<int> serverFd_set;
static default_random_engine e(time(0));
static discrete_distribution<int> d;
uint32_t default_event = EPOLLIN ;
IgnoreSigPipe initObj;
pii servers_inner[SERVERS_NUM];
struct epoll_event events_[MAX_EVENT];
int CurSelectedServerIndex = -1;
condition_variable cond;
mutex mtx;
char send_for_mon = 'G';
char rec[24];
vector<double> cpu_usage[SERVERS_NUM];
vector<double> memory_usage[SERVERS_NUM];
vector<double> io_usage[SERVERS_NUM];


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

void setReusePort(int socketFd, bool on) 
{
    int optval = on ? 1 : 0;
    if(::setsockopt(socketFd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof optval) < 0)
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

int socket_connect_mon(const struct sockaddr_in& client_addr_inner)
{
    auto cfd = socket(AF_INET, SOCK_STREAM, 0);
        if(cfd < 0)
        {
            perror("mon socket");
            exit(1);
        }
        struct sockaddr_in tmp_addr;
        tmp_addr.sin_family = AF_INET;
        tmp_addr.sin_port = htons(MON_PORT);
        tmp_addr.sin_addr.s_addr = client_addr_inner.sin_addr.s_addr;
        int ret = connect(cfd, (struct sockaddr* )&tmp_addr, sizeof(tmp_addr));
        if(ret != 0) 
        {
            perror("mon connnect");
            exit(1);
        }
        return cfd;
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

void EpollCtl_Add(int epollFd, int fd, uint32_t _event_) 
{
    struct epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = _event_;
    if( epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) != 0) 
    {
        perror("epoll_ctl add");
        exit(1);
    }
}

void CloseConnect(int epollFd, int fd)
{
    if( epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) != 0) 
    {
        perror("epoll_ctl del");
        exit(1);
    }
    close(fd);
}

void set_distribution()
{
    lock_guard<mutex> locker(mtx);
    d = discrete_distribution<int>(p.begin(), p.end());
}

int get_curSelectedServer_fd () 
{
    int index = 0;
    {
        lock_guard<mutex> locker(mtx);
        index = d(e);
    }
    return servers_inner[index].first;
}

void threadFunc() 
{
    cout << "threadFunc start!!!"<<endl;
    char buf[normalLen];
    message mess = {0};
    const char correct_res[normalLen] = {'a'};
    const char err_res[normalLen] = {'b'};
    int readn;
    int writen;
    int listenFd_outer = GetListenFd(OUTER_PORT, 512);
    setNonblock(listenFd_outer);

    int epollFd = epoll_create(1024);
    assert(epollFd >= 0);
    
    EpollCtl_Add(epollFd, listenFd_outer, default_event);

    while(1) 
    {
        
        int n = epoll_wait(epollFd, events_, MAX_EVENT, -1);

        for(int i = 0; i < n; ++i) {
            if(events_[i].data.fd == listenFd_outer) 
            {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len;
                while(1) 
                {
                    auto newConnectionFd = accept(listenFd_outer, (struct sockaddr* )&client_addr, &client_addr_len);
                    if(newConnectionFd < 0 ) 
                    {
                        if(errno == EAGAIN) break;
                        else 
                        {
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
                
                if(!serverFd_set.count(fd)) 
                {
                    readn = read(fd , buf, normalLen);
                    if( readn == 0 || (readn < 0 && errno != EAGAIN)) CloseConnect(epollFd, fd);
                    else if(readn == normalLen) 
                    {   
                        mess.fd = fd;
                        memcpy(mess.m, buf, normalLen);
                        int CurSelectedServer_fd = get_curSelectedServer_fd();
                        writen = write(CurSelectedServer_fd, &mess, messLen);
                        if(writen != messLen)
                            write(fd, err_res, normalLen);
                    }
                    else {
                        write(fd, err_res, normalLen);
                    }
                }
                else
                {
                    readn = read(fd , &mess, messLen);
                    if(readn <= 0) 
                    {
                        perror("inner server closed!");
                        exit(1);
                    }

                    int clientFd = mess.fd;
                    assert(clientFd > 0);

                    if(readn != messLen) 
                        write(clientFd, err_res, normalLen);
                    else 
                        write(clientFd, correct_res, normalLen);
                }
            }   
        }
    }
    close(epollFd);
    close(listenFd_outer);
}

void init_inner_mon () 
{
    int listenFd_inner = GetListenFd(INNER_PORT, SERVERS_NUM);
    for(int i = 0; i < SERVERS_NUM; ++i) 
    {
        struct sockaddr_in client_addr_inner;
        socklen_t client_addr_inner_len;
        auto newConnectionFd = accept(listenFd_inner, (struct sockaddr* )&client_addr_inner, &client_addr_inner_len);
        if(newConnectionFd < 0) 
        {
            perror("inner connnect!");
            exit(1);
        }
        int mon_fd = socket_connect_mon(client_addr_inner);
        setTcpNoDelay(mon_fd, 1);
        setTcpNoDelay(newConnectionFd, 1);
        servers_inner[i] = {newConnectionFd, mon_fd};
        serverFd_set.insert(newConnectionFd);
    }
    cout<<"init_inner_mon finished!!"<<endl;
}

void mainThreadFunc() 
{
    double cpu = 0, memory = 0, io = 0;
    cout << "mainThradFunc start!!!"<<endl;
    while(1) {
        sleep(2);
        for(int i = 0; i < SERVERS_NUM; ++i) 
        {
            auto mon_fd = servers_inner[i].second;
            write(mon_fd, &send_for_mon, 1);

            if( (read(mon_fd, rec, 3*sizeof(double))) != 3*sizeof(double) ) 
            {
                perror("mon read <> 3*sizeof(double)");
                exit(1);
            }

            double* dat = (double*)rec;
            cpu = *dat;
            memory = *(dat + 1);
            io = *(dat + 2);
            if(cpu >= 95 || memory >= 95 || io >= 95) 
                p[i] = 0.0;
            else 
                p[i] = 0.6 * (100.0 - cpu) + 0.3 * (100.0 - memory)  + 0.1 * (100.0 - io);
            cpu_usage[i].push_back(cpu);
            memory_usage[i].push_back(memory);
            io_usage[i].push_back(io);
        }
        set_distribution();
        if(cpu_usage[0].size() >= 100) 
        {
            for(int i = 0; i < SERVERS_NUM; ++i) 
            {
                string NumOfServer = to_string(i);
                string f1 = "server_" + NumOfServer + "_cpu_usage_";
                string f2 = "server_" + NumOfServer + "_memory_usage_";
                string f3 = "server_" + NumOfServer + "_io_usage";
                ofstream ofs1(f1, ios::out);
                ofstream ofs2(f2, ios::out);
                ofstream ofs3(f3, ios::out);
                for(int j = 0; j < cpu_usage[i].size(); ++j) 
                {
                    ofs1 << cpu_usage[i][j] << ",";
                    ofs2 << memory_usage[i][j] << ",";
                    ofs3 << io_usage[i][j] << ",";
                }
                ofs1.close();
                ofs2.close();
                ofs3.close();
            }
            cout << "finished!!" << endl;
            exit(1);
        }
    }
}


int main(int argc, char* argv[])
{
    
    init_inner_mon();
    set_distribution();
    thread(threadFunc).detach();

    mainThreadFunc();

    return 0;
}