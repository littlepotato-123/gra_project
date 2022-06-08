#include<sstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include<errno.h>
#include<memory>
#include<string.h>
#include<vector>
#include<iostream>
using namespace std;

class Procmon {
public:
    struct StatData
    {
        void parse(const char* startAtState)
        {
            std::istringstream iss(startAtState);
            string tmp;
            long long  user,nice,sys,idle,iowait,irq,softirq;
            iss >> tmp >> user >> nice >> sys >> idle >> iowait >> irq >> softirq;
            idle0 = idle;
            utime = user;
            systime = sys;
            all_time = user+nice+sys+idle+irq+softirq;
        }
        // int pid;
       
       long long idle0;
       long long utime, systime;
       long long all_time;   
    };

    struct UpData
    {
        void parse(const char* upinfo)
        {
            std::istringstream iss(upinfo);
            iss >> all_times;
        }
        // int pid;
       double all_times;
    };

    struct MemData
    {
        void parse(const char* meminfo)
        {
            std::istringstream iss(meminfo);
            string tmp;
            string kb;
            long long  MemTotal, MemFree, Buffers, Cached, MemAvailable;
            iss >> tmp >> MemTotal >> kb;
            iss >> tmp >> MemFree >> kb;
            iss >> tmp >> MemAvailable >> kb;
            iss >> tmp >> Buffers >> kb;
            iss >> tmp >> Cached >> kb;
            mem_idle = MemFree + Buffers + Cached;
            all_mem = MemTotal;
        }
        // int pid;
       long long mem_idle;
       long long all_mem;   
    };

    

struct IoData
    {
        void parse(const char* ioinfo)
        {
            std::istringstream iss(ioinfo);
            long long p1, p2;
            string name;
            long long tmp;
            while(!iss.eof()) {
               // cout<<"1"<<endl;
                iss >> p1 >> p2 >> name;
                for(int i  = 1; i <= 17; ++i) {
                    iss >> tmp;
                    if(i == 10 && (name.back() < '0' || name.back() > '9')) io_run_ += tmp;
                }
            }
        }
        // int pid;
       long long io_run_ = 0;
          
    };
//   struct CpuTime
//   {
//     int all_time;
//     int idle;
//     // double cpuUsage(double kPeriod, double kClockTicksPerSecond) const
//     // {
//     //   return (userTime_ + sysTime_) / (kClockTicksPerSecond * kPeriod) ;
//     // }
//   };

    Procmon() : 
        ticked(false),
        kClockTicksPerSecond_(static_cast<int>(::sysconf(_SC_CLK_TCK))),
        kbPerPage_(static_cast<int>(::sysconf(_SC_PAGE_SIZE))/1024) 
    {
        //cout<<kClockTicksPerSecond_<<endl;
        memset(&lastStatData_, 0, sizeof lastStatData_);
        memset(&lastIoData_, 0, sizeof lastIoData_);
    }


    tuple<double,double, double> tick()
    {

        StatData statData;
        memset(&statData, 0, sizeof statData);

        MemData memData;
        memset(&memData, 0, sizeof memData);

        IoData ioData;
        memset(&ioData, 0, sizeof ioData);

        UpData upData;
        memset(&upData, 0, sizeof upData);

        string meminfo = readProcFile("meminfo");
        string up_time = readProcFile("uptime");
        string io_stat = readProcFile("diskstats");
        string stat = readProcFile("stat");
        if(stat.empty() || meminfo.empty() || io_stat.empty()|| up_time.empty()) {
            cout<<"empty"<<endl;
            return  {0.0,0.0,0.0};
        }
        
        
        memData.parse(meminfo.data());
        statData.parse(stat.data());
        ioData.parse(io_stat.data());
        upData.parse(up_time.data());
        if(ticked) 
        {
            sec = upData.all_times - lastUpDatat_.all_times;
            auto all_time_diff = statData.all_time - lastStatData_.all_time;
            auto idle_diff = statData.idle0 - lastStatData_.idle0;
            //auto busy_time = all_time_diff - idle_diff;
            auto busy_time = statData.utime + statData.systime - lastStatData_.utime - lastStatData_.systime;
            cur_cpu_usage = (double)busy_time / (sec * kClockTicksPerSecond_ * hexin); 

            auto membusy = memData.all_mem - memData.mem_idle;
            cur_mem_usage = (double)membusy / memData.all_mem;

            double real_io_s = (ioData.io_run_- lastIoData_.io_run_)/ 1000.0;
            cur_io_usage = real_io_s / sec;
        }
        lastStatData_ = statData;
        lastIoData_ = ioData;
        lastUpDatat_ = upData;
        ticked = true;
        //cout<<memData.all_mem<<"   "<<memData.mem_idle<<endl;
        return {cur_cpu_usage, cur_mem_usage, cur_io_usage};
    }
private:
    bool  ticked;
    double sec = 0.0;
    double cur_cpu_usage = 0.0;
    double cur_mem_usage = 0.0;
    double cur_io_usage = 0.0;
    const int hexin = 2;
   // const double total_memory = 4194304.0;
    StatData lastStatData_;
    IoData lastIoData_;
    UpData lastUpDatat_;
    string readProcFile(const char* basename)
    {
        char filename[256];
        snprintf(filename, sizeof filename, "/proc/%s", basename);
        string content;
        char buf[BUFSIZ] = {'\0'};
        int fd = open(filename, O_RDONLY);
        if(fd < 0){
            perror("open");
            exit(1);
        }
        int n ;
        while(1){
            n = ::read(fd, buf, sizeof buf);
            if(n > 0) content.append(buf, n);
            else break;
        }  
        close(fd);
        return content;
    }
    const int kbPerPage_;
    const int kClockTicksPerSecond_;
};

int main(int argc, char* argv[]) {

    Procmon pm;
    while(1) {
        auto t = pm.tick();
        printf("cpu: %-.1f \t memory: %-.1f \t io_usage: %-.1f\n ", std::get<0>(t) * 100, std::get<1>(t)*100, std::get<2>(t) > 1 ? 100 :  std::get<2>(t) *100);
        sleep(3);
    }

}