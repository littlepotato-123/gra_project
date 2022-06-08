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
        void parse(const char* startAtState, int kbPerPage)
        {
            std::istringstream iss(startAtState);

            iss >> state;
            iss >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags;
            iss >> minflt >> cminflt >> majflt >> cmajflt;
            iss >> utime >> stime >> cutime >> cstime;
            iss >> priority >> nice >> num_threads >> itrealvalue >> starttime;
            long long  vsize, rss;
            iss >> vsize >> rss >> rsslim;
            vsizeKb = vsize / 1024;
            rssKb = rss * kbPerPage;
        }
        // int pid;
        char state;
        int ppid;
        int pgrp;
        int session;
        int tty_nr;
        int tpgid;
        int flags;

        long long  minflt;
        long long  cminflt;
        long long  majflt;
        long long  cmajflt;

        long long  utime;
        long long  stime;
        long long  cutime;
        long long  cstime;

        long long  priority;
        long long  nice;
        long long  num_threads;
        long long  itrealvalue;
        long long  starttime;

        long long  vsizeKb;
        long long  rssKb;
        long long  rsslim;
    };

  struct CpuTime
  {
    int userTime_;
    int sysTime_;
    double cpuUsage(double kPeriod, double kClockTicksPerSecond) const
    {
      return (userTime_ + sysTime_) / (kClockTicksPerSecond * kPeriod) / 1.0;
    }
  };

    Procmon(pid_t pid) : 
        ticked(false),
        pid_(pid),
        kClockTicksPerSecond_(static_cast<int>(::sysconf(_SC_CLK_TCK))),
        kbPerPage_(static_cast<int>(::sysconf(_SC_PAGE_SIZE))/1024) 
    {
        memset(&lastStatData_, 0, sizeof lastStatData_);
    }


    pair<double,double> tick(double kP_)
    {
        string stat = readProcFile("stat");
        string name;
        if(stat.empty()) {
            cout<<"empty"<<endl;
            return {0.0,0.0};
        }
        size_t lp = stat.find('(');
        size_t rp = stat.rfind(')');
        if (lp != string::npos && rp != string::npos && lp < rp)
        {
            name.assign(stat.data()+lp+1, static_cast<int>(rp-lp-1));
        }
        StatData statData;
        memset(&statData, 0, sizeof statData);
        statData.parse(stat.data() + rp + 1, kbPerPage_);
        if(ticked) 
        {
            CpuTime time;
            time.userTime_ = std::max(0, static_cast<int>(statData.utime - lastStatData_.utime));
            time.sysTime_ = std::max(0, static_cast<int>(statData.stime - lastStatData_.stime));
            //CpuTimeVec.push_back(time);
            cur_cpu_usage = time.cpuUsage(kP_ ,kClockTicksPerSecond_);
            cur_mem_usage = statData.rssKb / total_memory;
            //cout<<"111"<<endl;
        }
        lastStatData_ = statData;
        ticked = true;
        //cout<<name<< "    "<< lastStatData_.rssKb << "    "<<lastStatData_.rsslim<<endl;
        return {cur_cpu_usage, cur_mem_usage};
    }
private:
    bool  ticked;
    double cur_cpu_usage = 0.0;
    double cur_mem_usage = 0.0;
    const double total_memory = 4194304.0;
    //const static int kPeriod_ = 2;
    StatData lastStatData_;
    string readProcFile(const char* basename)
    {
        char filename[256];
        snprintf(filename, sizeof filename, "/proc/%d/%s", pid_, basename);
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
        return content;
    }
    const pid_t pid_;
    const int kbPerPage_;
    const int kClockTicksPerSecond_;
};

int main(int argc, char* argv[]) {
    if(argc < 2) {
        cout << "请输入要监控的进程id"<<endl;
        exit(1);
    }

    stringstream ss(argv[1]);
    int pid ;
    ss >> pid;

    Procmon pm(pid);
    while(1) {
        auto p = pm.tick(3.0);
        printf("cpu: %-.8f \t memory : %-.8f\n ", p.first * 100, p.second*100 );
        sleep(3);
    }

}