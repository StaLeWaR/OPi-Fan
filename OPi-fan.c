// g++ OPi-fan.c  -o OPi-fan  -lpthread -lwiringPi

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wiringPi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#define nil NULL
#define PID_FILE    ((const char *)"/run/OPi-fan.pid")
#define SOCK_FILE   ((const char *)"/run/OPi-fan.sock")


#define PIN_PWM       6      //PIN 6, PA06, Physical OPi-pin 7
#define PIN_RPM       7      //PIN 7, PA7, Physical OPi-pin 29
#define HALL_PULSE    2      //Number of pulses per one rotation FAN
#define TEMP_MIN      35     //Minimal CPU temperature
#define TEMP_MAX      60     //Maximum CPU temperature
#define POWER_MIN     5      //Minimal power percent for FAN
#define TIME_SLEEP    3      //Reaction time of the program


int currentTemp = 0 ;
int powerPercent = 0 ;
int rpm = 0 ;
int sockDaemon, sockClient;
volatile int pulseCounter = 0;
pthread_t readerTemp, pwmFaner, rpmFaner, daemonMessager;

void signal_handler(int sig) {
    pthread_cancel(daemonMessager);
    pthread_cancel(readerTemp);
    pthread_cancel(pwmFaner);
    pthread_cancel(rpmFaner);
    wiringPiSetup () ;
    digitalWrite (PIN_PWM, LOW) ;
    close(sockDaemon);
    unlink(SOCK_FILE) ;
    unlink(PID_FILE);
    exit(0) ;
}

void signal_init(void) {
    signal(SIGHUP, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGABRT, signal_handler);
    signal(SIGKILL, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);

}


void rpmInterrupt(void) {
          pulseCounter++;
}


void readTemp(void) {
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    FILE *cpuTempFile;
    char buff[12] ;
    while(1) {
        cpuTempFile = fopen("/sys/class/thermal/thermal_zone0/temp", "rb");
        fgets (buff, 12, cpuTempFile);
        currentTemp = atoi(buff) ;
        fclose(cpuTempFile);
        sleep(TIME_SLEEP);
   }
}

void pwmFan(void) {
      pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
      wiringPiSetup () ;
      pinMode (PIN_PWM, OUTPUT) ;
      digitalWrite (PIN_PWM, HIGH) ;   // On fan for first run
      delayMicroseconds(500000);
      int t_on, t_off, d, i ;
      while (1)  {
            d = powerPercent ;
            t_on=50*d;         // time power on
            t_off=50*(100-d);  // time power off
            for ( i=0; i!=(TIME_SLEEP*1000000)/(t_on+t_off); i++) {
               digitalWrite (PIN_PWM, HIGH) ;   // On
               delayMicroseconds(t_on);
               digitalWrite (PIN_PWM, LOW) ;    // Off
               delayMicroseconds(t_off);
            }
      }
}

void rpmFan(void) {
      pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
      wiringPiSetup () ;
      pinMode(PIN_RPM, INPUT);
      pullUpDnControl (PIN_RPM, PUD_UP) ;
      wiringPiISR (PIN_RPM, INT_EDGE_FALLING, &rpmInterrupt);
      while (1)  {
        rpm=(pulseCounter/HALL_PULSE)*60 ;
        pulseCounter = 0;
        delayMicroseconds ( 1000000 ); //cheсk every 1 second
      }
}

void daemonMessage(void) {
    int  msgsock;
    struct sockaddr_un server;
    char msg[256];

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    sockDaemon = socket(AF_UNIX, SOCK_STREAM, 0);
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, SOCK_FILE);
    bind(sockDaemon, (struct sockaddr *) &server, sizeof(struct sockaddr_un));
    listen(sockDaemon, 5);
    while(1) {
        msgsock = accept(sockDaemon, 0, 0);
        if (msgsock != -1) {
            sprintf (msg, "%d %d %d", currentTemp, powerPercent, rpm);
            write(msgsock, msg, sizeof(msg));
         }
        close(msgsock);
    }
}

void setPidFile(void) {
    FILE* pfd;
    pfd = fopen(PID_FILE, "w+");
    if (pfd)
    {
        fprintf(pfd, "%u", getpid());
        fclose(pfd);
        printf("PID = %d, writing in %s\n",getpid(),PID_FILE);
    }
}


void fanCotrolDaemon(void) {
    signal_init();
    setPidFile();
    pthread_create(&readerTemp, nil, (void *(*)(void *)) readTemp, nil);
    pthread_create(&pwmFaner, nil, (void *(*)(void *)) pwmFan, nil);
    pthread_create(&rpmFaner, nil, (void *(*)(void *)) rpmFan, nil);
    pthread_create(&daemonMessager, nil, (void *(*)(void *)) daemonMessage, nil);
    while (1) {
        if (currentTemp > TEMP_MAX){
           powerPercent = 100 ;
        } else {
          if (currentTemp > TEMP_MIN) {
            if (((currentTemp - TEMP_MIN)*(100/(TEMP_MAX - TEMP_MIN)))  < POWER_MIN  ) {
                 powerPercent = POWER_MIN ;
            } else {
                 powerPercent = (currentTemp - TEMP_MIN)*(100/(TEMP_MAX - TEMP_MIN));
            }
          } else {
            powerPercent = POWER_MIN ;
          }
        }
        printf("Temp = %d, Power percent = %d, RPM = %d\n", currentTemp, powerPercent, rpm);
        sleep(TIME_SLEEP);
    }
}


int checkStatus(void) {
    int pfd;
    if ((pfd = open(PID_FILE, O_RDONLY)) < 0) {
        return 1; //PID File not found. Not running
    } else {
        return 0; //PID File found. Daemon running
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        printf("Error! Please run program with key -h for read help\n");
        return 0;
    }
    int opt;
    while((opt = getopt(argc, argv, "idhs")) != -1) {
        switch(opt) {
             case 'i':
                if ( checkStatus() == 0) {
                   printf("Error! The daemon OPi-fan is already running!\n");
                } else {
                   printf ("Running Interactive mode\n");
                   fanCotrolDaemon();
                }
                break;
             case 'd':
                if ( checkStatus() == 0) {
                   printf("Error! The daemon OPi-fan is already running!\n");
                } else {
                   pid_t parpid, sid;
                   parpid = fork();
                   if(parpid < 0) {
                        printf("Daemon start failed\n");
                        exit(1);
                   } else if(parpid != 0) {
                        printf("Daemon starting\n");
                        exit(0);
                   }
                   umask(0);
                   setsid();
                   chdir("/");
                   close(STDIN_FILENO);
                   close(STDOUT_FILENO);
                   close(STDERR_FILENO);
                   fanCotrolDaemon();
                }
                break;
             case 's':
                if ( checkStatus() == 0) {
                    struct sockaddr_un server;
                    char buf[256];
                    sockClient = socket(AF_UNIX, SOCK_STREAM, 0);
                    server.sun_family = AF_UNIX;
                    strcpy(server.sun_path, SOCK_FILE);
                    if (connect(sockClient, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) < 0) {
                        close(sockClient);
                        printf ("Error! No connecting to socket OPi-fan");
                        exit(1);
                    }
                    bzero(buf, sizeof(buf));
                    read(sockClient, buf, 256);
                    printf("%s\n", buf);
                    close(sockClient);
                    break;
                } else {
                    printf("Error! The daemon OPi-fan is not running!\n");
                }
                 break;
             case 'h':
                 printf("usage: OPi-fan -i -d -s [-h]\n");
                 printf("  Options include: \n");
                 printf("  -i                        = running in interactive mode\n");
                 printf("  -d                        = running in background mode\n");
                 printf("  -s                        = show data from background mode\n");
                 printf("                              Format output: TEMP POWER RPM\n");
                 printf("  -h                        = help message\n");
                 break;
        }
    }
    return 0;
}
