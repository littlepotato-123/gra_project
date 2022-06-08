
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define PROGRAM_VERSION "1.5"
volatile int timerexpired=0;
int speed=0;
int failed=0;
uint64_t diff = 0;
//int bytes=0;

uint16_t service_id = 1;
uint16_t method_id = 2;
uint16_t client_id = 3;
uint16_t session_id = 4;
uint8_t message_type = 1;
int port =9999;

int mypipe[2];
int benchtime = 30;
int clients = 1;
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 16
char request[REQUEST_SIZE];




uint64_t now()
{
  struct timeval tv = { 0, 0 };
  gettimeofday(&tv, NULL);
  return tv.tv_sec * ((uint64_t)(1000000)) + tv.tv_usec;
}

static const struct option long_options[] = 
{
    {"time",required_argument,NULL,'t'},
    {"serverid",required_argument,NULL,'S'},
    {"methodid",required_argument,NULL,'M'},
    {"clientid",required_argument,NULL,'C'},
    {"sessionid",required_argument,NULL,'E'},
    {"help",no_argument,NULL,'?'},
    {"version",no_argument,NULL,'V'},
    {"clients",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};

static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
    timerexpired = 1;
}

static void usage(void) 
{
    fprintf(stderr, 
            "someip_bench [option]... URL\n"
            "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
            "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
            "  -S|--serverid <n>        输入服务id\n"
            "  -M|--methodid <n>        输入方法id\n"
            "  -C|--clientid <n>        输入客户端id\n"
            "  -E|--sessionid <n>       输入会话id\n"
            "  -?|-h|--help             This information.\n"
            "  -V|--version             Display program version.\n");
}

int someip_socket(const char* host, int clientPort) {
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent* hp;

    memset(&ad, 0, sizeof(ad));

    ad.sin_family = AF_INET;

    inaddr = inet_addr(host);
    if(inaddr != INADDR_NONE) 
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        hp = gethostbyname(host);
        if(!hp) return -1;
        
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }

    ad.sin_port = htons(clientPort);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if(sock < 0) return sock;
    if(connect(sock, (struct sockaddr* )&ad, sizeof(ad)) < 0)
        return -1;

    return sock;
}


int main(int argc, char* argv[]) {
    int opt = 0;
    int options_index = 0;
    char* tmp = NULL;

    if(argc == 1) {
        usage();
        return 2;
    }

    while( (opt = getopt_long(argc, argv, "VS:M:C:E:t:c:?h",long_options,&options_index)) !=EOF ) {
        switch(opt) {
            case 0 : break;
            case 'V' : printf(PROGRAM_VERSION"\n") ;exit(0);
            case 't' : benchtime=atoi(optarg);break;
            case 'S' : service_id=atoi(optarg);break;
            case 'M' : method_id=atoi(optarg);break;
            case 'C' : client_id=atoi(optarg);break;
            case 'E' : session_id=atoi(optarg);break;
            case ':' :
            case '?' :
            case 'h' : usage();return 2;break;
            case 'c' : clients=atoi(optarg);break;
        }
    }

    if(optind == argc) {
        fprintf(stderr, "someip_bench: Missing URL!\n");
        usage();
        return 2;
    }
    if(clients == 0) clients = 1;
    if(benchtime == 0) benchtime = 30;

    fprintf(stderr," Someip_Bench by Bill- "PROGRAM_VERSION"\n");

    build_request(argv[optind]);

    printf("Runing info: ");

    if(clients == 1) printf("1 client");
    else printf("%d clients", clients);

    printf(", running %d sec\n", benchtime);
    
    return bench();

}

void build_request(const char *url) {
    char tmp[10];
    int i;

    memset(host, 0, MAXHOSTNAMELEN);
    memset(request, 0, REQUEST_SIZE);
    if(strchr(url,'/')==NULL) {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    strncpy(host, url, strchr(url,':') - url);
    memset(tmp, 0, 10);
    strncpy(tmp, index(url, ':')+1, strchr(url,'/') - index(url, ':') - 1);
    port = atoi(tmp);
    //if(port==0)port = 9527;

    //操作request
    service_id = htons(service_id);
    method_id = htons(method_id);
    client_id = htons(client_id);
    session_id = htons(session_id);
    uint16_t* cur = (uint16_t*)request;
    *cur++ = service_id;
    *cur++ = method_id;
    *cur++ = htons(0);
    *cur++ = htons(8);
    *cur++ = client_id;
    *cur++ = session_id;
    cur = (char*) cur;
    *cur++ = 1;
    *cur++ = 1;
    *cur++ = message_type;
    *cur++ = 0;
}

static int bench(void) {
    pid_t pid = 0;
    FILE* f;
    int i,j;
    uint64_t k;
    i = someip_socket(host, port);
    if(i < 0) {
        fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);

    if(pipe(mypipe)) {
        perror("pipe failed!");
        return 3;
    }

    for(i = 0; i < clients; ++i) {
        pid = fork();
        if(pid <= (pid_t) 0) {
            sleep(1);
            break;
        }
    }
    
    if(pid < 0) {
        fprintf(stderr,"problems forking worker no. %d\n",i);
        perror("fork failed.");
        return 3;
    }

    if(pid == 0) {
        benchcore(host, port, request);
        f = fdopen(mypipe[1],"w");
        if(!f) {
            perror("open pipe for writing failed.");
            return 3;
        }

        fprintf(f,"%d %d %llu\n",speed,failed,diff);
        fclose(f);

        return 0;
    }

    else {
        f=fdopen(mypipe[0],"r");
        if(f==NULL) 
        {
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(f,NULL,_IONBF,0);
        speed = 0;
        failed = 0;
        diff = 0;

        while(1) {
            pid = fscanf(f, "%d %d %llu", &i, &j, &k);
            if(pid < 3) {
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }

            speed += i;
            failed += j;
            diff += k;
            if(--clients == 0) break;
        }
        fclose(f);
        double response_time = (diff/1000.0)/speed;
        printf("\nSpeed=%d pages/min\nRequests: %d susceed, %d failed.average response time of susceed: %.2lf ms\n",
            (int)((speed+failed)/(benchtime/60.0f)),
            speed,
            failed,
            response_time);
    }
    return i;
}

void benchcore(const char *host,const int port,const char *req) {
    speed = 0;
    failed = 0;
    diff = 0;
    
    int rlen = 16;
    char buf[16];
    uint64_t inner_diff = 0;
    uint64_t start = 0;
    int s;
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if(sigaction(SIGALRM, &sa, NULL)) exit(3);
    alarm(benchtime);

    while(1) {
        if(timerexpired) {
            if(failed > 0) --failed;
            return;
        }
        s = someip_socket(host, port);
        if(s < 0) { ++failed; continue;}
        if(rlen != write(s, req, rlen)) { ++failed;close(s);continue;}
        start = now();
        if(shutdown(s, 1)) { ++failed;close(s);continue;}
        if(read(s, buf, 16) < 16) { ++failed;close(s);continue;}
        inner_diff = now() - start;
        if(buf[0] != 'a' || buf[10] !='a' || buf[15] != 'a') { ++failed;close(s);continue;}
        if(close(s)) {++failed;continue;}
        ++speed;
        diff += inner_diff;
    }
}