#include<iostream>
#include<math.h>

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include<vector>

#include<sstream>
#include <stdlib.h>
#include <fcntl.h>
#include<errno.h>
#include<memory>
#include<string.h>

using namespace std;
int main(){
    cout<< getpid() << endl;
    // while(1){
    //     for(int i = 0; i < 1e9; ++i){
    //         auto b = sqrt(i) ;
    //     }
    //     sleep(2);
    // }
    // vector<int > v1(3e8 + 1e8, 1235);

    // while(1) {
    //     for(int i = 0; i < 1e9; ++i) {

    //     }
    //     for(int i = 0; i < 1e9; ++i) {
            
    //     }
    //     for(int i = 0; i < 1e9; ++i) {
            
    //     }
    //     for(int i = 0; i < 1e9; ++i) {
            
    //     }
    //     usleep(70000);
    // }
    
    //1024*1024*1024
    const long long  len = 1024*1024*512;
    auto buf = new char[len];
    memset(buf, 'a', len);
    while(1) {

        int fd = open("iotestt", O_RDWR | O_CREAT, 0755);
        if(fd < 0) {
            perror("open1");
            exit(1);
        }
        
        while(1) {
            long long n= 0, l = len;
            while(l > 0) {
                n = write(fd, buf, l);
                l -= n;
            }
            lseek(fd,0, SEEK_SET);
            usleep(10000);
        }

        close(fd);
    }
    
}