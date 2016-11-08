#include <glib.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "socket.h"
#define EPOLL_MAXEVENTS 10
#define SOCKET_NUM 10

int to_quit = 0;
static int count;
static int timeout = 1000;
static int max_count = SOCKET_NUM * 3;
int efd = -1;

int epoll_register(int events, int efd, int socket) {
    struct epoll_event ev;
        ev.events = events;
        ev.data.fd = socket;
        printf("register socket: %d\n", socket);
        if (epoll_ctl(efd, EPOLL_CTL_ADD, socket, &ev) == -1) {
            return 0;
        }
        return 1;
}

void accept_data(int fd) {
        char buf[1024];
        char *data = NULL;
        int done = 0;
        int valid_data = 0;
        int count = 0;
        do {
            count = read(fd, buf, sizeof(buf));
            if (count > 0) {
                buf[count] = '\0';
                if (data) {
                    char *p = data;
                    asprintf(&data, "%s%s", p, buf);
                    free(p);
                } else {
                    asprintf(&data, "%s", buf);
                }
            }
        } while (count == sizeof(buf));
        if (data) {
            printf("received data: \n%s\n", data);
            free(data);
        }
}

void write_data(int fd, const char* data, ...)
{
   int i = 0;
   char str[strlen(data)+200];
   va_list argptr;
   va_start(argptr, data);
   vsprintf(str, data, argptr);
   va_end(argptr);
   size_t data_len = strlen(str);
   if (write(fd, str, data_len) != data_len) {
       fprintf(stderr, "partial/failed write\n");
       exit(EXIT_FAILURE);
   }
}

void epoll_monitor(int efd, int timeout) {
    struct epoll_event events[EPOLL_MAXEVENTS];
    int n = epoll_wait(efd, events, EPOLL_MAXEVENTS, timeout);
    if (n <= 0) {
        to_quit = 1;
        return;
    }
    while (n-- > 0) {
        if (events[n].events & EPOLLIN) {
            accept_data(events[n].data.fd);
        }
        if ((events[n].events & EPOLLOUT) && count < max_count) {
            write_data(events[n].data.fd, "%d: something from %d\n", count++, events[n].data.fd);
        }
        if (events[n].events & EPOLLRDHUP) {
            printf("server  socket is closed\n");
            to_quit = 1;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((efd = epoll_create1(EPOLL_CLOEXEC))==-1) {
        fprintf(stderr, "epoll setup is failed \n");
        exit(EXIT_FAILURE);
    }

    int socket[SOCKET_NUM];
    int i = 0;
    for (i = 0; i < SOCKET_NUM; i++) {
        socket[i] = setup_client_socket(argv[1], argv[2]);
        if (!epoll_register(EPOLLIN|EPOLLRDHUP|EPOLLOUT|EPOLLET, efd, socket[i])) {
            fprintf(stderr, "add client socket to epoll is failed");
            exit(EXIT_FAILURE);
        }
        sleep(1);
    }
//    for (i = 0; i < SOCKET_NUM; i++) {
//        write_data(socket[i], "%d: something from socket[%d]: %d\n", count++, i, socket[i]);
//    }
    //int max_count = SOCKET_NUM * 3;
    while(!to_quit) {
        epoll_monitor(efd, timeout);
    }
    for (i = 0; i < SOCKET_NUM; i++) {
        close(socket[i]);
    }
    close(efd);
}
