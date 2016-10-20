#include <glib.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "socket.h"
#define EPOLL_MAXEVENTS 10

int efd = -1;

int epoll_register(int events, int efd, int socket) {
    struct epoll_event ev;
        ev.events = EPOLLIN|EPOLLRDHUP;
        ev.data.fd = socket;
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
            buf[count] = '\0';
            if (data) {
                char *p = data;
                asprintf(&data, "%s%s", p, buf);
                free(p);
            } else {
                asprintf(&data, "%s", buf);
            }
        } while (count == sizeof(buf));
        if (data) {
            printf(data);
            free(data);
        }
}

void epoll_monitor(int efd, int timeout) {
    struct epoll_event events[EPOLL_MAXEVENTS];
    int n = epoll_wait(efd, events, EPOLL_MAXEVENTS, timeout);
    while (n-- > 0) {
        printf("got epoll event: %d, events: %x\n", n, events[n].events);
        if (events[n].events & EPOLLIN) {
            accept_data(events[n].data.fd);
        }
    }
}

void write_data(int *fd, int fd_num, char *data)
{
   int i = 0;
   char str[strlen(data)+20];
   for (; i < fd_num; i++) {
       sprintf(str, "%s from %d\n", data, fd[i]);
       size_t data_len = strlen(str);
       if (write(fd[i], str, data_len) != data_len) {
           fprintf(stderr, "partial/failed write\n");
           exit(EXIT_FAILURE);
       }
   }
}

#define SOCKET_NUM 10
int to_quit = 0;
int main(int argc, char* argv[])
{
        if (argc != 3) {
                fprintf(stderr, "Usage: %s ip port\n", argv[0]);
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
            if (!epoll_register(EPOLLIN|EPOLLRDHUP, efd, socket[i])) {
                fprintf(stderr, "add client socket to epoll is failed");
                exit(EXIT_FAILURE);
            }
        }
        int timeout = 1000;
        int count = 0; 
        while(!to_quit) {
            epoll_monitor(efd, timeout);
            write_data(socket, SOCKET_NUM, "something");
            count++;
            if (count > 30) {
//                write_data(socket, SOCKET_NUM, "quit");
                to_quit = 1;
            }
        }
        for (i = 0; i < SOCKET_NUM; i++) {
            close(socket[i]);
        }
        close(efd);
}
