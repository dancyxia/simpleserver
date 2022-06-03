This project is to explain how epoll is used to implement a simple TCP server.

### TCP server

As a TCP server, it implements at least following functions:

1. It can accept connections from multiple clients

2. It can handle data passed from clients and return corresponding data to the clients

To set up connections and do the communication, socket goes to the play. In network programming, a socket is like a file handler, just instead of representing a file, it represents a pairing endpoint in the network. When writing or reading from a socket, the data are passed to or from this paring endpoint. 
One server can have thousands of connections. How to manage such huge number of connections? Have a thread or process for each connection? It's not an ideal solution as effort to manage shared resources is challenging. Linux offered a notification facility called epoll which enables to manage the multiple connections in one thread.

### socket
For a TCP server, socket set consists of functions socket(), bind() and accept(). See the following code for how to use them.

```
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
        if (!make_non_block_socket(fd)) {
            goto error_out;
        }
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

error_out:      
		close(fd);
        fd = -1;
    }
	
    if (fd == -1) {
        fprintf(stderr, "socket is not established!\n");
        exit(EXIT_FAILURE);
    }

    if (ori_res)
        freeaddrinfo(ori_res);

    return fd;
}

void accept_connection(int socket, accept_connection_callback_t callback)
{
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
        if (!callback(infd)) {
            goto error_out;
        }
    } else {
        goto error_out;
    }

    return;
error_out:
    close(infd);
}

```

### EPOLL 
epoll set consists of three system call. epoll_create, epoll_ctl and epoll_wait.

#### epoll_create and epoll_create1

{%  highlight c linenos  %}
#include <sys/epoll.h>
int epoll_create(int size);int epoll_create1(int flags);
{% endhighlight %}

epoll_create() or epoll_create1() is to create and return a file descriptor referring to the created epoll instance. It serves as the epoll interface for subsequent epoll calls. epoll_create was added to the kernel in version 2.6. epoll_create1() was added to the kernel in version 2.6.27. The size parameter for epoll_create() is original designed as the initial number of the file descriptors to be added to the epoll instance. As the design change, now this value is no longer used. However, to be back compatible, the value needs to be greater than 0.

for epoll_create1(), argument flags can include EPOLL_CLOEXEC to set the close-on-exec (FD_CLOEXEC) flag on the new file descriptor. 

the fd created by epoll_create must be closed with close(fd) to release the allocated resources when it's no longer used.


#### epoll_ctl

{%  highlight c linenos  %}
#include <sys/epoll.h>

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
{% endhighlight %}

funciton epoll_ctl() is used to control epoll operations. 

The first argument is the fd of the epoll instance.
The second argument specify the epoll operation. The valid operations are EPOLL_CTL_ADD, EPOLL_CTL_MOD and EPOLL_CTL_DEL. 
The third argument is the file descriptor to be worked on. The event argument describes the object linked to the file descriptor fd. 
The struct epoll_event is defined as 

{%  highlight c linenos  %}
typedef union epoll_data {
    void        *ptr;
    int          fd;
    uint32_t     u32;
    uint64_t     u64;
} epoll_data_t;

struct epoll_event {
    uint32_t     events;      /* Epoll events */
    epoll_data_t data;        /* User data variable */
};
{% endhighlight %}

The events member is a bit set composition. The example only handle events EPOLLIN, EPOLLET and EPOLLRDHUP

#### epoll_wait

```
#include <sys/epoll.h>

int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout);
int epoll_pwait(int epfd, struct epoll_event *events,
               int maxevents, int timeout,
               const sigset_t *sigmask);
```

The function epoll_wait and epoll_pwait waits for events on the epoll(7) instance referred to by the file descriptor epfd. The memory events pointer point to stores the events information. The argument maxevents must be greater than 0. Argument timeout specifies the 
minimum number of milliseconds that epoll_wait() will block. specifying it as -1 cause the epoll_wait() to block infinitely, while 0 makes the epoll_wait() to return immediately. 

#### edge trigger or level trigger

By default, epoll event is distributed as level trigger. That is, as long as data is there, the epoll event is triggered. For instance, the peer sends 30 bytes to the server. If the server reads 10 bytes and return. Next time when epoll_wait() is called, the EPOLLIN event is triggered again. But in edge trigger mode, this is not the case. In edge trigger mode, only when the state is changed from not ready to ready, can the epoll event be triggered. For above example, after 10 bytes were read, calling epoll_wait() again can not be returned with EPOLLIN event. It might cause the infinite block. Therefore, for a ET epoll, all data should be read to make the next epoll_wait availabe.

#### example

```
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
        int socket = setup_server_socket(argv[1]);
        if (!epoll_register(EPOLLIN|EPOLLRDHUP|EPOLLET, efd, socket)) {
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
        }
        close(socket);
        close(efd);
        close_queue();
        printf("exit!\n");
}

int epoll_register(int events, int efd, int socket) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = socket;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, socket, &ev) == -1) {
        return 0;
    }
    return 1;
}

void epoll_monitor(int efd, int socket, int timeout) {
    struct epoll_event events[EPOLL_MAXEVENTS];
    int n = epoll_wait(efd, events, EPOLL_MAXEVENTS, timeout);
    while (n-- > 0) {
        if (events[n].events & EPOLLRDHUP) {
            printf("peer socket is closed\n");
            release_epoll(events[n].data.fd);
        } else if (events[n].events & EPOLLIN) {
            if (events[n].data.fd == socket) { //listening socket
                printf("accept_connection");
                accept_connection(socket, &register_epoll);
            } else { //get data
                accept_data(events[n].data.fd);
            }
        }
    }
}
```
