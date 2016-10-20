#include "socket.h"

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

    return fd;
}

int setup_client_socket(char *ip, char *port)
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
    int result = getaddrinfo(ip, port, &hints, &ori_res);
    if (result != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result));
        exit(EXIT_FAILURE);
    }

    int fd=-1;
    int on=1;
    for (res=ori_res; res; res=res->ai_next) {
        fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd == -1) continue;
//        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
//            fprintf(stderr, "set SO_REUSEADDR failed: %m");
//            goto error_out;
//        }
//
//        //TCP_DEFER_ACCEPT (since Linux 2.4)
//        //        Allow a listener to be awakened only when data arrives on the
//        //        socket.  Takes an integer value (seconds), this can bound the
//        //        maximum number of attempts TCP will make to complete the
//        //        connection.  This option should not be used in code intended
//        //        to be portable.
//        //http://unix.stackexchange.com/questions/94104/real-world-use-of-tcp-defer-accept/94120#94120
//        if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &on, sizeof(on)) == -1) {
//            fprintf(stderr, "set TCP_DEFER_ACCEPT failed: %m");
//            goto error_out;
//        }
//        if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
//            fprintf(stderr, "set NONBLOCK failed: %m");
//            goto error_out;
//
//        }
//        //It marks the file descriptor so that it will be close()d automatically when the process
//        //or any children it fork()s calls one of the exec*() family of functions. This is useful
//        //to keep from leaking your file descriptors to random programs run by e.g. system()
//        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
//            fprintf(stderr, "set CLOSE_ON_EXEC flag failed: %m");
//            goto error_out;
//        }
        if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
                fprintf(stderr, "connect failed \n");
                goto error_out;
        }

        printf("connection is established!\n");
        break;

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

int make_non_block_socket(int fd)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        fprintf(stderr, "set NONBLOCK failed: %m");
        return 0;
    }
    return 1;
}

void accept_connection(int socket, accept_connection_callback_t callback)
{
    struct sockaddr in_addr;
    socklen_t in_len = sizeof in_addr;
    int infd;
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    printf("\n accept_connection\n");
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
