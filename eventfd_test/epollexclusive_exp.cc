
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
#include <errno.h>
using namespace std;

int epolls[10] ;
int main() {
    
    int evfd = eventfd(10, EFD_NONBLOCK);
    //cout<<"evfd :"<<evfd<<endl;
    //sleep(1);
    for(int i =  0; i < 10; ++i) {
        thread([evfd,i](){
            int myN = i;
            vector<struct epoll_event> events_(10);
            int epollfd = epoll_create(512);
            epolls[i] = epollfd;
            epoll_event ev = {0};
            ev.data.fd = evfd;
            ev.events = EPOLLIN | EPOLLEXCLUSIVE;
            epoll_ctl(epollfd, EPOLL_CTL_ADD, evfd, &ev);
            while(1){
                int n = epoll_wait(epollfd, &events_[0], static_cast<int>(events_.size()), -1);
                for(int i = 0 ; i < n; ++i ){
                    auto fd = events_[i].data.fd;
                    auto eve = events_[i].events;
                    if(eve & EPOLLIN) {
                        cout<<"这是in事件\t线程id是:"<<myN<<"事件fd是:"<<fd<<"   ";
                        uint64_t a;
                        int tmp;
                        while( (tmp = read(fd, &a, 8)) > 0) {
                            cout<<"read : "<< a << endl;
                        }
                        if(tmp == -1)cout<<"读了-1"<<endl;
                        
                        // epoll_event ev = {0};
                        // ev.data.fd = evfd;
                        // ev.events = EPOLLIN | EPOLLET;
                        // epoll_ctl(epollfd, EPOLL_CTL_MOD, evfd, &ev);
                    }
            }
            }
        }).detach();
    }
    sleep(3);
    

    // epoll_event ev = {0};
    // ev.data.fd = evfd;
    // ev.events = EPOLLIN |EPOLLEXCLUSIVE;
    // epoll_ctl(epolls[3], EPOLL_CTL_ADD, evfd, &ev);

    for(int i = 0; i < 1000; ++i){
        uint64_t w = i;
        // if(i == 5) {
        //     epoll_ctl(epolls[3], EPOLL_CTL_DEL, evfd, 0);
        // }
        write(evfd, &w, 8);
        usleep(100000);
    }
    
    getchar();
}