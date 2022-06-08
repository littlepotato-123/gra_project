#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<fcntl.h>
#include<netpacket/packet.h>
#include<net/if.h>
#include<net/if_arp.h>
#include<netinet/in.h>
#include<netinet/ip.h>
#include<linux/if_ether.h>
#include<arpa/inet.h>
#include<sys/ioctl.h>
#include<unistd.h>

int main(int argc, char **argv) {
   int sock, n,i;
   char buffer[2048];
   char tmp[1024];
   unsigned char *iphead, *ethhead, *tcp, *someip, *src_port, *dest_port, *ip_len, *tcp_head_len;
   struct ifreq ethreq;
  



   int fd = socket(AF_INET, SOCK_STREAM, 0);
   
   struct sockaddr_in serv_addr;
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_port = htons(9527);
   inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
   if((connect(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0)) {
     perror("connet ");
     exit(1);
   }
   fcntl(fd, F_SETFL, O_NONBLOCK);

   
   if ( (sock=socket(PF_PACKET, SOCK_RAW,
                     htons(ETH_P_IP)))<0) {
     perror("socket");
     exit(1);
   }

   /* Set the network card in promiscuos mode */
   strncpy(ethreq.ifr_name,"ens33",IFNAMSIZ);
   if (ioctl(sock,SIOCGIFFLAGS,&ethreq)==-1) {
     perror("ioctl");
     close(sock);
     exit(1);
}
   ethreq.ifr_flags|=IFF_PROMISC;
   if (ioctl(sock,SIOCSIFFLAGS,&ethreq)==-1) {
     perror("ioctl");
     close(sock);
     exit(1);
   }


      // struct packet_mreq mr;

      // memset(&mr,0,sizeof(mr));
      // mr.mf_ifindex = dev_id;

      // mr.mr_type = PACKET_MR_PROMISC; // 用于激活混杂模式以接受所有网络包;

      // sock = socket(PF_PACKET, SOCK_RAW, ETH_P_ALL);  // 所有类型的报文

      // setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP,&mr,sizeof(mr));

 
   while (1) {
     
     n = recvfrom(sock,buffer,2048,0,NULL,NULL);


     iphead = buffer+14; /* Skip Ethernet header */

     if(  (int)(iphead[12]) == 127  ) continue; 
     ip_len = iphead + 2;//16位，表示ip报文一共有多少字节
     tcp_head_len = iphead + 32; //4位，表示有多少个4字节
      
     int tcp_len = (ip_len[0] << 8 ) + ip_len[1] - 20; 
     int tcp_head_len_int = (tcp_head_len[0] >> 4) * 4;
     int someip_len = tcp_len - tcp_head_len_int;
     if (*iphead==0x45 && ( someip_len > 0) && (((iphead[22]<<8)+iphead[23]) == 9527) ) { 
      char* cur = buffer + 54;
      while(cur < buffer + n) {
        int length_int = ( *(cur + 6) << 8) + *(cur + 7);
        
         for(int i = 0; i < 8 + length_int ; ++i) printf("%02x ", cur[i]);
         printf("\n");
        
        int ret ;
        if( (ret = write(fd, cur, 8 + length_int) ) < 0) {
          if(errno == EAGAIN) break;
          perror("write");
          exit(1);
        }
        
        cur += 8 + length_int;
        
      }
     }
   }
}
