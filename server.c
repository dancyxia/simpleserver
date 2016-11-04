#include <glib.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#ifdef PI
#include <wiringPi.h>
#endif
#include "socket.h"
#define EPOLL_MAXEVENTS 10
#define  LedPin   21

typedef struct data {
        gchar *buf;
        int size;
        int fd;
} socket_data;

//typedef struct data_list {
//        socket_data *data;
//        struct data_list *next;
//} socket_data_list;

//typedef struct {
//    int fd;
//    socket_data_list *data_list;
//} epoll_socket_data;

#define POOL_SIZE 15
#define MAX_FD    10
//typedef struct {
//    int pool[POOL_SIZE];
//    int free_pool[POOL_SIZE];
//    int pool_idx_list[MAX_FD];
//    int free_pool_end;
//    int need_compress;
//} fd_pool;

static int HANDLE_LMT_PER_VISIT = 3;

sig_atomic_t to_quit = 0;
GQueue in_queue = G_QUEUE_INIT;
GQueue out_queue = G_QUEUE_INIT;
//static fd_pool fdpool = {.free_pool_end=POOL_SIZE-1, .need_compress = 0};

//void init_fd_pool() {
//    memset(fdpool.pool, -1, POOL_SIZE);
//    for (int i = 0; i < POOL_SIZE; i++) {
//        fdpool.free_pool[i] = i;
//    }
//}

int efd = -1;

void quit() {
    to_quit = 1;
}

//int compress_free_pool(void)
//{
//    if (!fdpool.need_compress)
//        return 0;
//    int s = 0, e = POOL_SIZE-1;
//    while (s < e) {
//        if (fdpool.pool[fdpool.free_pool[e]] != -1) {
//            e--;
//        } else {
//            int t = fdpool.free_pool[e];
//            fdpool.free_pool[e] = fdpool.free_pool[s];
//            fdpool.free_pool[s] = t;
//            s++;
//        }
//    }
//
//    fdpool.free_pool_end = e;
//    return e >= 0;
//}

int epoll_register(int events, int efd, int socket) {
//    if (fdpool.free_pool_end < 0 && !compress_free_pool()) {
//        fprintf(stderr, "no resources are available for registering new poll fd. free_pool_end: %d", fdpool.free_pool_end);
//        return 0;
//    }

//    int new_pool_idx = fdpool.free_pool[fdpool.free_pool_end--];
    //socket_data_list *node = (socket_data_list*)g_slice_alloc(sizeof socket_data_list);
    //node->
//    epoll_socket_data *data = (epoll_socket_data*)g_slice_alloc(sizeof(epoll_socket_data));
//    data->fd = socket;
//    data->data_list = NULL;
    struct epoll_event ev;
    ev.events = EPOLLIN|EPOLLRDHUP;
//    ev.data.ptr = data;
    ev.data.fd = socket;
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

void free_data_with_fd(gpointer data_ptr, gpointer user_data)
{
    socket_data *data = data_ptr;
    int *fd = user_data;

    if (*fd == data->fd) {
        data->fd = -1;
       //free_data(data); 
    }
}

void release_epoll(int fd)
{
    g_queue_foreach (&in_queue, free_data_with_fd, &fd);
    g_queue_foreach (&out_queue, free_data_with_fd, &fd);
    close(fd);
}

//void release_epoll(epoll_socket_data *data_ptr)
//{
//        socket_data_list *node = data_ptr->data_list;
//
//        while(data_ptr->data_list != NULL) {
//                node = data_ptr->data_list->next;
//                free_data(data_ptr->data_list->data);
//                g_slice_free1(sizeof(socket_data_list), data_ptr->data_list);
//                data_ptr->data_list = node;
//
//        }
//        data_ptr->data_list;
//        close(data_ptr->fd);
//}

void accept_data(int fd) {
        char buf[1024];
        int done = 0;
        socket_data *data = calloc(1, sizeof(socket_data));
        data->fd = fd;
        int valid_data = 0;
        while(1) {
                int count = read(fd, buf, sizeof(buf));
                if (count == -1) { //error reading
                        if (errno != EAGAIN) {
                                done = 1;
                                fprintf(stderr, "read error\n");
                        } else {
                                valid_data = 1;
                                g_queue_push_tail(&in_queue, data);
//                                socket_data_list * node = g_slice_alloc(sizeof(socket_data_list));
//                                node->next = data_ptr->data_list;
//                                data_ptr->data_list = node;
                        }
                        break;
                } else if (count == 0){ //client is closed
                        done = 1;
                        break;
                } else {
                        buf[count] = '\0';
                        if (data>buf) {
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
//            printf("close fd: %d\n", fd);
            release_epoll(fd);
        }
}

int register_epoll(int infd)
{
    if (!epoll_register(EPOLLIN|EPOLLRDHUP|EPOLLET, efd, infd)) {
        fprintf(stderr, "epoll register for accepting socket is failed\n");
        return 0;
    }
    return 1;
}

void epoll_monitor(int efd, int socket, int timeout) {
    struct epoll_event events[EPOLL_MAXEVENTS];
    int n = epoll_wait(efd, events, EPOLL_MAXEVENTS, timeout);
    while (n-- > 0) {
        if (events[n].events & EPOLLIN) {
            printf("events in, idx: %d, socket: %d\n", events[n].data.fd, events[n].data.fd);
            if (events[n].data.fd == socket) { //listening socket
            //if (((epoll_socket_data*)events[n].data.ptr)->fd == socket) { //listening socket
                printf("accept_connection");
                accept_connection(socket, &register_epoll);
            } else { //get data
                //printf("accept_data\n");
                accept_data(events[n].data.fd);
            }
        }
    }
}

#ifdef PI
static void handle_gpio(char id)
{
    static int flashing = 0, status = LOW, initialized = 0;
    if (id == 0x30) {
        if (!initialized) {
            if(wiringPiSetup() == -1){ //when initialize wiring failed,print messageto screen
                printf("setup wiringPi failed !");
                return 1;
            }
            printf("linker LedPin : GPIO %d(wiringPi pin)\n",LedPin); //when initialize wiring successfully,print message to screen
            pinMode(LedPin, OUTPUT);
            initialized = 1;
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
#endif

void handle_in_queue()
{
    int n = g_queue_get_length(&in_queue);

    if (!to_quit && n > HANDLE_LMT_PER_VISIT) {
        n = HANDLE_LMT_PER_VISIT;
    }
    int i = 0;
    for (; i < n; i++) {
        socket_data * data = (socket_data*)g_queue_pop_head(&in_queue);
        if (data->fd == -1) {
            free_data(data);
            continue;
        }
        //TODO: quit when receive the signal
        if (!strncmp(data->buf, "quit", 4)) {
            free_data(data);
            quit();
        } else if (!strncmp(data->buf, "gpio", 4)) {
#ifdef PI
            handle_gpio(strlen(data->buf) < 6 ? 0 : data->buf[5]);
            free_data(data);
#endif
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
        if (data->fd == -1) {
            write(1, data->buf, strlen(data->buf));
        } else if (send(data->fd, data->buf, strlen(data->buf), 0) < 0) {
            release_epoll(data->fd);
            close(data->fd);
        }
        free_data(data);
    }
}

static const char welcome[]="welcome to my world";

static void prepare_welcome_msg() {
        socket_data *data = calloc(1, sizeof(socket_data));
        asprintf(&data->buf, "%s\n", welcome);
        data->size = sizeof(welcome);
        data->fd = -1;
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

        if ((efd = epoll_create1(EPOLL_CLOEXEC))==-1) {
                fprintf(stderr, "epoll setup is failed \n");
                exit(EXIT_FAILURE);
        }
        //init_fd_pool();
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
#ifdef PI
                handle_gpio(0);
#endif
        }
        close(socket);
        close(efd);
        close_queue();
        printf("exit!\n");
}
