/*
 * Copyright (c) 2009-2014, The Regents of the University of California,
 * through Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights reserved.
 *
 * This code is distributed under a BSD style license, see the LICENSE file
 * for complete information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <netinet/tcp.h>


#include "iperf.h"
#include "iperf_api.h"
#include "units.h"
#include "locale.h"
#include "net.h"


#define BUF_SIZE 100
// #define PORT 1234 /* Open Port on Remote Host */
#define MAXDATASIZE 100 /* Max number of bytes of data */

extern const char *inet_ntop (int __af, const void *__restrict __cp,
                  char *__restrict __buf, socklen_t __len);


static int run(struct iperf_test *test);
void host_test();
void client_test(char hostaddr[50]);
void addhost(char *stru[] ,char host[],char *gv[],int c);
static int count=0;


void gethost(char hostaddr[100]){
        setvbuf(stdout, NULL, _IONBF, 0); 
    fflush(stdout); 
 
    int sock = -1;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
    {   
    
    }   
         
    const int opt = 1;
    //设置该套接字为广播类型，
    int nb = 0;
    nb = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&opt, sizeof(opt));

 
    struct sockaddr_in addrto;
    bzero(&addrto, sizeof(struct sockaddr_in));
    addrto.sin_family=AF_INET;
    addrto.sin_addr.s_addr=htonl(INADDR_BROADCAST);
    addrto.sin_port=htons(6000);
    int nlen=sizeof(addrto);

    struct sockaddr_in from;
    bzero(&from, sizeof(struct sockaddr_in));
    from.sin_family = AF_INET;
    from.sin_addr.s_addr = htonl(INADDR_ANY);
    from.sin_port = htons(6000);

    char buffer[100] = {0};

    sleep(1);
        //从广播地址发送消息
        char smsg[] = {"abcdef"};
        int ret=sendto(sock, smsg, strlen(smsg), 0, (struct sockaddr*)&addrto, nlen);
        printf("+++++++++");
        int strLen = recvfrom(sock,hostaddr, BUF_SIZE, 0, (struct sockaddr*)&from, nlen);

       
        char fstr[16];

        inet_ntop(AF_INET,&(from.sin_addr.s_addr), fstr, sizeof(struct sockaddr_in));
            printf("%s\t",hostaddr);  
}



/************************************************************************/
int
main(int argc, char **argv)
{ 

char hostaddr[100];
gethost(hostaddr);

printf("%s\n",hostaddr);
// host_test();
sleep(2);
client_test(hostaddr);

   
    return 0;
}

/**************************************************************************/


void addhost(char *stru[] ,char host[],char *gv[],int c){



    for (int i = 0; i < c+1; ++i)
    {
    if(i<2){

gv[i]=strdup(stru[i]);
    }
    else if(i==2){

        gv[i]=strdup(host);
        ++count;
    }
    else{
      
        gv[i]=strdup(stru[i-1]);
    }

    }

return;
}


void host_test(){
    struct iperf_test *test;
    test = iperf_new_test();
    iperf_defaults(test);
    iperf_set_test_may_use_sigalrm(test, 1);
    
char *gv[]={"iperf3","-s"};

    if (iperf_parse_arguments(test, 2, gv) < 0) {
        iperf_err(test, "parameter error - %s", iperf_strerror(i_errno));
        fprintf(stderr, "\n");
        usage_long();
        exit(1);
    }

    iperf_run_server(test);
 

    iperf_free_test(test);
    sleep(2);
    // client_test();
    return 0;

}
void client_test(char hostaddr[50]){
  struct iperf_test *test;
    test = iperf_new_test();
    iperf_defaults(test);
    iperf_set_test_may_use_sigalrm(test, 1);


    char *stru[]={"iperf3","-c","-t","3"};
    


    char *gv[5];
    printf("%s\n",hostaddr);
        addhost(stru,hostaddr,gv,4);



    if (iperf_parse_arguments(test, 5, gv) < 0) {
        iperf_err(test, "parameter error - %s", iperf_strerror(i_errno));
        fprintf(stderr, "\n");
        usage_long();
        exit(1);
        }
       if (iperf_run_client(test))
       {
           /* code */
       }


    iperf_free_test(test);
    // host_test();
    return 0;
}






static int
run(struct iperf_test *test)
{
    int consecutive_errors;

    switch (test->role) {
        case 's':
        if (test->daemon) {
        int rc = daemon(0, 0);
        if (rc < 0) {
            i_errno = IEDAEMON;
            iperf_errexit(test, "error - %s", iperf_strerror(i_errno));
        }
        }
        consecutive_errors = 0;
            for (;;) {
                if (iperf_run_server(test) < 0) {
            iperf_err(test, "error - %s", iperf_strerror(i_errno));
                    fprintf(stderr, "\n");
            ++consecutive_errors;
            if (consecutive_errors >= 5) {
                fprintf(stderr, "too many errors, exiting\n");
            break;
            }
                } else
            consecutive_errors = 0;
                iperf_reset_test(test);
            }
            break;
        case 'c':
            if (iperf_run_client(test) < 0)
        iperf_errexit(test, "error - %s", iperf_strerror(i_errno));
            break;
        default:
            usage();
            break;
    }

    return 0;
}
