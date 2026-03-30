#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT "9000"
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

static int server_fd = -1;
static int client_fd = -1;
static int running = 1;

void signal_handler(int sig)
{
    (void)sig;
    syslog(LOG_INFO, "Caught signal, exiting");
    running = 0;
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    remove(DATAFILE);
    closelog();
    exit(0);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
        daemon_mode = 1;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed");
        freeaddrinfo(res);
        return -1;
    }

    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind failed");
        freeaddrinfo(res);
        close(server_fd);
        return -1;
    }
    freeaddrinfo(res);

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "fork failed");
            return -1;
        }
        if (pid > 0) exit(0);
        setsid();
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "listen failed");
        close(server_fd);
        return -1;
    }

    while (running) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (running) syslog(LOG_ERR, "accept failed");
            continue;
        }

        char ip[INET6_ADDRSTRLEN];
        inet_ntop(client_addr.ss_family,
                  get_in_addr((struct sockaddr *)&client_addr),
                  ip, sizeof(ip));
        syslog(LOG_INFO, "Accepted connection from %s", ip);

        // Receive data and append to file
        int file_fd = open(DATAFILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "open failed");
            close(client_fd);
            continue;
        }

        char *buf = malloc(BUFSIZE);
        if (!buf) {
            syslog(LOG_ERR, "malloc failed");
            close(file_fd);
            close(client_fd);
            continue;
        }

        ssize_t bytes;
        int packet_complete = 0;
        while (!packet_complete && (bytes = recv(client_fd, buf, BUFSIZE, 0)) > 0) {
            write(file_fd, buf, bytes);
            for (ssize_t i = 0; i < bytes; i++) {
                if (buf[i] == '\n') {
                    packet_complete = 1;
                    break;
                }
            }
        }
        free(buf);
        close(file_fd);

        // Send file content back to client
        file_fd = open(DATAFILE, O_RDONLY);
        if (file_fd != -1) {
            char *rbuf = malloc(BUFSIZE);
            if (rbuf) {
                ssize_t rbytes;
                while ((rbytes = read(file_fd, rbuf, BUFSIZE)) > 0)
                    send(client_fd, rbuf, rbytes, 0);
                free(rbuf);
            }
            close(file_fd);
        }

        shutdown(client_fd, SHUT_WR);
        syslog(LOG_INFO, "Closed connection from %s", ip);
        close(client_fd);
        client_fd = -1;
    }

    close(server_fd);
    remove(DATAFILE);
    closelog();
    return 0;
}
