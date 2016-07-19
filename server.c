#include <glib.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netdb.h>
#include <stdio.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>

#include <wiringPi.h>

#define  LedPin   21 
#define BACKLOG 10
#define EPOLL_MAXEVENTS 10

typedef struct data {
        gchar *buf;
        int size;
        int pool_idx;
} socket_data;

#define POOL_SIZE 100
#define MAX_FD    10
typedef struct {
    int pool[POOL_SIZE];
    int free_pool[POOL_SIZE];
    int pool_idx_list[MAX_FD];
    int free_pool_end;
} fd_pool;

static int HANDLE_LMT_PER_VISIT = 3;

sig_atomic_t to_quit = 0;
GQueue in_queue = G_QUEUE_INIT;
GQueue out_queue = G_QUEUE_INIT;
static fd_pool fdpool;


void quit() {
        to_quit = 1;
}

int setup_server_socket(char *port)
{
        struct addrinfo hints;
        struct addrinfo *res, *ori_res;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
        hints.ai_protocol = SOL_TCP;          /* Any protocol */
        hints.ai_canonname = NULL;
        hints.ai_addr = NULL;
        hints.ai_next = NULL;
        int result = getaddrinfo(NULL, port, &hints, &ori_res);
        if (result != 0) {
                fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
                exit(EXIT_FAILURE);
        }

        int fd=-1;
        int on=1;
        for (res=ori_res; res; res=res->ai_next) {
                fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
                if (fd == -1) continue;
                if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
                        fprintf(stderr, "set SO_REUSEADDR failed: %m");
                        goto error_out;
                }

                //SO_REUSEPORT (since Linux 3.9)
                //        Permits multiple AF_INET or AF_INET6 sockets to be bound to
                //        identical socket address.  This option must be set on each
                //        including the first socket) prior to calling bind(2)
                //        socket.  To prevent port hijacking, all of the
                //        to the same address must have the same
                //        effective UID.  This option can be employed with both TCP and
                //        UDP sockets.
//                if (setsockopt(fd, SOL_TCP, SO_REUSEPORT, &on, sizeof(on)) == -1) {
//                        fprintf(stderr, "set SO_REUSEPORT failed: %m");
//                        goto error_out;
//                }
                //TCP_DEFER_ACCEPT (since Linux 2.4)
                //        Allow a listener to be awakened only when data arrives on the
                //        socket.  Takes an integer value (seconds), this can bound the
                //        maximum number of attempts TCP will make to complete the
                //        connection.  This option should not be used in code intended
                //        to be portable.
                //http://unix.stackexchange.com/questions/94104/real-world-use-of-tcp-defer-accept/94120#94120
                if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &on, sizeof(on)) == -1) {
                        fprintf(stderr, "set TCP_DEFER_ACCEPT failed: %m");
                        goto error_out;
                }
                if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
                        fprintf(stderr, "set NONBLOCK failed: %m");
                        goto error_out;

                }
                //It marks the file descriptor so that it will be close()d automatically when the process
                //or any children it fork()s calls one of the exec*() family of functions. This is useful
                //to keep from leaking your file descriptors to random programs run by e.g. system()
                if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
                        fprintf(stderr, "set CLOSE_ON_EXEC flag failed: %m");
                        goto error_out;
                }
                if (bind(fd, res->ai_addr, res->ai_addrlen) == 0) {
                        if (listen(fd, BACKLOG) == -1) {
                                fprintf(stderr, "listen failed \n");
                                goto error_out;
                        }
                        break;
                }

error_out:      close(fd);
                fd = -1;
        }
        if (fd == -1) {
                fprintf(stderr, "socket is not established!\n");
                exit(EXIT_FAILURE);
        }

        if (ori_res)
                freeaddrinfo(ori_res);

        memset(&fdpool, 0, sizeof(fd_pool));
        fdpool.free_pool_end = POOL_SIZE - 1;
        return fd;
}

int make_non_block_socket(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        fprintf(stderr, "set NONBLOCK failed: %m");
        return 0;
    }
    return 1;
}

int epoll_register(int events, int efd, int socket) {
    if (fdpool.free_pool_end == 0) {
        fprintf(stderr, "no resources are available for registering new poll fd. free_pool_end: %d", fdpool.free_pool_end);
    }

    int new_pool_idx = fdpool.free_pool[fdpool.free_pool_end--];
    struct epoll_event ev;
        ev.events = EPOLLIN|EPOLLRDHUP;
        ev.data.fd = new_pool_idx;
        fdpool.pool[new_pool_idx] = socket;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, socket, &ev) == -1) {
                return 0;
        }
        return 1;
}

void free_data(socket_data *data) {
        if (data == NULL)
                return;
        if (data->buf != NULL) {
                free(data->buf);
        }
        free(data);
}


void accept_connection(int efd, int socket) {
        struct sockaddr in_addr;
        socklen_t in_len = sizeof in_addr;
        int infd;
        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        infd = accept(socket, &in_addr, &in_len);
        if (infd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                        fprintf(stderr, "epoll accept failed");
                }
                return;
        }

        if (getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST|NI_NUMERICSERV) == 0) {
                printf("Accepted service is on host %s, port %s \n", hbuf, sbuf);
        }

        //add fd to epoll monitor
        if (make_non_block_socket(infd)) {
            if (!epoll_register(EPOLLIN|EPOLLRDHUP|EPOLLET, efd, infd)) {
                fprintf(stderr, "epoll register for accepting socket is failed\n");
                goto error_out;
            }
        } else {
            goto error_out;
        }

        return;
error_out:
          close(infd);
}

void accept_data(int fdidx) {
        char buf[1024];
        int done = 0;
        socket_data *data = calloc(1, sizeof(socket_data));
        data->pool_idx = fdidx;
        int fd = fdpool.pool[fdidx];
        int valid_data = 0;
        while(1) {
                int count = read(fd, buf, sizeof(buf));
                if (count == -1) { //error reading
                        if (errno != EAGAIN) {
                                done = 1;
                                printf(stderr, "read error\n");
                        } else {
                                valid_data = 1;
                                g_queue_push_tail(&in_queue, data);
                        }
                        break;
                } else if (count == 0){ //client is closed
                        done = 1;
                        break;
                } else {
                        buf[count] = '\0';
                        if (data>buf != NULL) {
                                char *p = data->buf;
                                asprintf(&data->buf, "%s%s", p, buf);
                                free(p);
                        } else {
                                asprintf(&data->buf, "%s", buf);
                        }
                        data->size += count;
                }
        }
        if (!valid_data) {
                free_data(data);
        }
        if (done) {
                close(fd);
                fdpool.pool[fdidx] = -1;
        }
}

void epoll_monitor(int efd, int socket, int timeout) {
        struct epoll_event events[EPOLL_MAXEVENTS];
        int n = epoll_wait(efd, events, EPOLL_MAXEVENTS, timeout);
        while (n-- > 0) {
                if (events[n].events & EPOLLIN) {
                    if (fdpool.pool[events[n].data.fd] == socket) { //listening socket
                                accept_connection(efd, socket);
                        } else { //get data
                                accept_data(events[n].data.fd);
                        }
                }
        }
}

static void handle_gpio(char id)
{
    static int flashing = 0, status = LOW, initalized = 0;

    if (id == 0x30) {
        if (!initalized) {
            if(wiringPiSetup() == -1){ //when initialize wiring failed,print messageto screen
                printf("setup wiringPi failed !");
                return 1; 
            }
            printf("linker LedPin : GPIO %d(wiringPi pin)\n",LedPin); //when initialize wiring successfully,print message to screen
            pinMode(LedPin, OUTPUT);
            initalized = 1;
        }
        flashing = 1;
    } else if (id == 0x31){
        status = HIGH;
        digitalWrite(LedPin, status);  //led off
        flashing = 0;
    }

    if (flashing) {
        digitalWrite(LedPin, status);  //led on
        status = status == LOW? HIGH : LOW;
        delay(500);
    }
}

void handle_in_queue()
{
        int n = g_queue_get_length(&in_queue);

        if (!to_quit && n > HANDLE_LMT_PER_VISIT) {
            n = HANDLE_LMT_PER_VISIT;
        }
        int i = 0;
        for (; i < n; i++) {
                socket_data * data = (socket_data*)g_queue_pop_head(&in_queue);
                //TODO: quit when receive the signal
                if (!strncmp(data->buf, "quit", 4)) {
                    free_data(data);
                    quit();
                } else if (!strncmp(data->buf, "gpio", 4)) {
                    handle_gpio(strlen(data->buf) < 6 ? 0 : data->buf[5]);
                } else if (!strncmp (data->buf, "get", 3)){
                    char *old_data = data->buf;
                    asprintf(&data->buf, "HTTP/1.1 200 ok\r\n\r\necho %s", old_data);
                    free(old_data);
                    data->size = strlen(data->buf);
                    g_queue_push_tail(&out_queue, data);
                } else {
                    g_queue_push_tail(&out_queue, data);
                }
#if 0
                if (!g_strncmp(data->buf, "echo", 4)) {
                        //TODO: only store the content to be echo-ed
                        //data->buf 
                        g_queue_push_tail(out_queue, data);
                } else if (!g_strncmp(data->buf, "quit", 4)) {
                        free_data(data);
                        //TODO: quit
                } else if (!g_strncmp(data->buf, "hello", 5)) {
                        socket_data *ret_data = calloc(1, sizeof(socket_data));
                        asprintf(ret_data->buf, "%s", "you");
                        ret_data->size = 3;
                }
#endif
        }
}

void handle_out_queue()
{
    int n = g_queue_get_length(&out_queue);
    if (!to_quit && n > HANDLE_LMT_PER_VISIT) {
        n = HANDLE_LMT_PER_VISIT;
    }
    int i = 0;
    for (; i < n; i++) {
        socket_data *data = g_queue_pop_head(&out_queue);
        if (data->pool_idx == -1) {
            write(1, data->buf, strlen(data->buf));
        } else {
            int fd= fdpool.pool[data->pool_idx];
            if (fd != -1) {
                if (send(fd, data->buf, strlen(data->buf), 0) < 0) {
                    close(fd);
                    fdpool.pool[data->pool_idx] = -1;
                } else {
                    write(1, data->buf, strlen(data->buf));
                }
            }
        }
        free_data(data);
    }
}

static const char welcome[]="welcome to my world";

static void prepare_welcome_msg() {
        socket_data *data = calloc(1, sizeof(socket_data));
        asprintf(&data->buf, "%s\n", welcome);
        data->size = sizeof(welcome);
        data->pool_idx = -1;
        g_queue_push_tail(&out_queue, data);
}

void close_queue(void)
{
    int n = g_queue_get_length(&out_queue);
    int i = 0;
    
    for (;i < n; i++) {
        free_data(g_queue_pop_head(&out_queue));
    }

    n = g_queue_get_length(&in_queue);
    for (i = 0;i < n; i++) {
        free_data(g_queue_pop_head(&in_queue));
    }
}
#if 0
static int
to_CIDR(const uint32_t* netmask, int8_t proto)
{
    int cidr = proto == AF_INET ? 32: 128;
    int group_num = proto == AF_INET ? 0:3;
    int i = group_num;
    uint32_t group_data;
    for (; i >= 0; i--) {
        group_data = netmask[i];
        while ((group_data & 0xff) == 0 && cidr > 0) {
            cidr -= 8;
            group_data >>= 8;
        }
    }

    if (cidr > 0) {
        while ((group_data & 1) == 0) {
            cidr--;
            group_data >>= 1;
        }
    }
    return cidr;
}
#endif
int main(int argc, char* argv[])
{
        if (argc != 2) {
                fprintf(stderr, "Usage: %s port\n", argv[0]);
                exit(EXIT_FAILURE);
        }

        int efd = -1;
        if ((efd = epoll_create1(EPOLL_CLOEXEC))==-1) {
                fprintf(stderr, "epoll setup is failed \n");
                exit(EXIT_FAILURE);
        }
        int socket = setup_server_socket(argv[1]);
        if (!epoll_register(EPOLLIN|EPOLLRDHUP, efd, socket)) {
                fprintf(stderr, "add server socket to epoll is failed");
                exit(EXIT_FAILURE);
        }
        prepare_welcome_msg();
        int timeout = 0;
        while(!to_quit) {
            timeout = g_queue_is_empty(&in_queue) && g_queue_is_empty(&out_queue) ? 1000:0;
                epoll_monitor(efd, socket, timeout);
                handle_in_queue();
                handle_out_queue();
                handle_gpio(0);
        }
        close(socket);
        close(efd);
        close_queue();
}
