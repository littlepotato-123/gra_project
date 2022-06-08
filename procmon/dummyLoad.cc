#include<atomic>
#include<condition_variable>
#include<thread>
#include<mutex>
#include<math.h>
#include<stdio.h>

using namespace std;

int g_cycles = 0;
int g_percent = 82;
atomic<int32_t> g_done;
bool g_busy = false;