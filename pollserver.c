#include <glib.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>

#include "socket.h"
#define EPOLL_MAXEVENTS 10
#undef MAX(a,b)
#define MAX(a,b) (                      \
        {  __typeof__ (a) _a = (a);         \
        __typeof__ (b) _b = (b);        \
        _a > _b ? _a : _b;})

#define POOL_SIZE 100
#define MAX_FD    10
typedef struct data {
    gchar *buf;
    int size;
    int pool_idx;
} socket_data;

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
int used_pollfd=0;
struct pollfd pfds[10];

void quit() {
    to_quit = 1;
}

void poll_register(int events, int socket) {
    if (used_pollfd >= MAX_FD || fdpool.free_pool_end == 0) {
        fprintf(stderr, "no resources are available for registering new poll fd. used poll: %d, free_pool_end: %d", used_pollfd, fdpool.free_pool_end);
    }
    pfds[used_pollfd].fd = socket;
    pfds[used_pollfd].events = events;
    int new_pool_idx_list = fdpool.free_pool[fdpool.free_pool_end--];
    fdpool.pool[new_pool_idx_list] = used_pollfd;
    fdpool.pool_idx_list[used_pollfd] = new_pool_idx_list;
    used_pollfd++;
}

void free_data(socket_data *data) {
    if (data == NULL)
        return;
    if (data->buf != NULL) {
        free(data->buf);
    }
    free(data);
}

void push_custom_msg(const char *msg)
{
    socket_data *data = calloc(1, sizeof(socket_data));
    asprintf(&data->buf, "%s\n", msg);
    data->size = strlen(msg);
    data->pool_idx = -1;
    g_queue_push_tail(&out_queue, data);
}


int accept_data(int fd_idx) {
    char buf[1024];
    int done = 0;
    socket_data *data = calloc(1, sizeof(socket_data));
    int valid_data = 0;
    int fd = pfds[fd_idx].fd;
    while(1) {
        int count = 0;
        ioctl(fd, FIONREAD, &count);
        printf("count: %d\n", count);
        if (count < 0) { //error reading
            done = 1;
            fprintf(stderr, "read error\n");
            break;
        } else if (count == 0) {
            if (valid_data) {
                g_queue_push_tail(&in_queue, data);
            } else {
                char buffer[32];
                if (recv(fd, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT) == 0) {         //client is closed
                    done =1;
                }
            }
            break;
        } else {
            valid_data = 1;
            data->pool_idx = fdpool.pool_idx_list[fd_idx];
            count = read(fd, buf, sizeof(buf));
            buf[count] = '\0';
            if (data->buf != NULL) {
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
        printf("client is closed\n");
        pfds[fd_idx].fd = -1;
        return 0;
    }
    return 1;
}

static void compress_fds()
{
    int i = 0;
    int j = 0;
    for (; j < used_pollfd; ) {
        if (pfds[j].fd != -1) {
            if (j != i) {
                pfds[i] = pfds[j];
                fdpool.pool[fdpool.pool_idx_list[j]] = i;
                fdpool.pool[fdpool.pool_idx_list[i]] = -1;
                fdpool.pool_idx_list[i] = fdpool.pool_idx_list[j];
                fdpool.pool_idx_list[j] = -1;
                pfds[j].fd = -1;
            }
            i++;
        }
        j++;
    }
    used_pollfd = i;
}

static int register_poll(int infd)
{
    poll_register(POLLIN, infd);
    return 1;
}

void poll_monitor(int socket, int timeout) {
    int rc = poll(pfds, used_pollfd, timeout);
    if (rc < 0) {
        fprintf(stderr, "Poll failed, error: %d\n", errno);
        quit();
    } else if (rc == 0) {//timeout
        return;
    }

    int i = 0;
    int need_compress = 0;
    for (; i < used_pollfd; i++) {
        if (pfds[i].revents & POLLIN) {
            if (pfds[i].fd == socket) {
                accept_connection(socket, &register_poll);
            } else {
                printf("get data from socket %d\n", i);
                if (!need_compress)
                    need_compress = accept_data(i);
            }
        }
    }

    if (need_compress) {
        compress_fds();
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
        } else {
            char *old_data = data->buf;
            //asprintf(&data->buf, "echo %s", old_data);
            asprintf(&data->buf, "HTTP/1.1 200 ok\r\n\r\necho %s", old_data);
            free(old_data);
            data->size = strlen(data->buf);
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
    int need_compress = 0;
    for (; i < n; i++) {
        socket_data *data = g_queue_pop_head(&out_queue);
        if (data->pool_idx == -1) {
            write(1, data->buf, strlen(data->buf));
        } else {
            int fd_idx = fdpool.pool[data->pool_idx];
            if (fd_idx != -1) {
                if (send(pfds[fd_idx].fd, data->buf, strlen(data->buf), 0) < 0) {
                    close(pfds[fd_idx].fd);
                    pfds[fd_idx].fd = -1;
                    compress_fds();
                } else {
                    write(1, data->buf, strlen(data->buf));
                }
            }
        }
        free_data(data);
    }
}

void close_client_sockets(void) 
{
    int i = 0;
    for (; i < used_pollfd; i++) {
        close(pfds[i].fd);
    }
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

static const char welcome[]="welcome to my world";

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int socket = setup_server_socket(argv[1]);
    memset(pfds, 0 , sizeof(pfds));
    poll_register(POLLIN, socket);
    push_custom_msg(welcome);
    int timeout = 0;
    while(!to_quit) {
        timeout = g_queue_is_empty(&in_queue) && g_queue_is_empty(&out_queue) ? 1000:0;
        poll_monitor(socket, timeout);
        handle_in_queue();
        handle_out_queue();
    }
    close_client_sockets();
    close(socket);
    close_queue();
}
