#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "config_parser.h"
#include "libmysyslog.h"

#define BUF_SZ 1024

static volatile sig_atomic_t g_stop = 0;

static void sig_handler(int signo) {
    g_stop = 1;
}

static int is_user_allowed(const char *username) {
    FILE *fp = fopen("/etc/myRPC/users.conf", "r");
    if (!fp) {
        mysyslog("Cannot open users.conf", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("users.conf");
        return 0;
    }
    char buf[256];
    int ok = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        buf[strcspn(buf, "\n")] = 0;
        if (buf[0] == '#' || buf[0] == 0) continue;
        if (strcmp(buf, username) == 0) {
            ok = 1;
            break;
        }
    }
    fclose(fp);
    return ok;
}

static void run_command(const char *cmd, char *out_file, char *err_file) {
    char syscmd[BUF_SZ * 2];
    snprintf(syscmd, sizeof(syscmd), "%s >%s 2>%s", cmd, out_file, err_file);
    system(syscmd);
}

static void handle_client(const char *req, char *resp, size_t resp_size) {
    char *req_copy = strdup(req);
    char *username = strtok(req_copy, ":");
    char *cmd = strtok(NULL, "");
    if (cmd) while (*cmd == ' ') ++cmd;

    if (is_user_allowed(username)) {
        mysyslog("User allowed", INFO, 0, 0, "/var/log/myrpc.log");
        char out_tmp[] = "/tmp/myRPC_XXXXXX.stdout";
        char err_tmp[] = "/tmp/myRPC_XXXXXX.stderr";
        mkstemp(out_tmp);
        mkstemp(err_tmp);
        run_command(cmd, out_tmp, err_tmp);

        FILE *f = fopen(out_tmp, "r");
        if (f) {
            size_t n = fread(resp, 1, resp_size - 1, f);
            resp[n] = 0;
            fclose(f);
            mysyslog("Command executed", INFO, 0, 0, "/var/log/myrpc.log");
        } else {
            snprintf(resp, resp_size, "Error reading stdout file");
            mysyslog("Error reading stdout file", ERROR, 0, 0, "/var/log/myrpc.log");
        }
        remove(out_tmp);
        remove(err_tmp);
    } else {
        snprintf(resp, resp_size, "1: User '%s' is not allowed", username);
        mysyslog("User not allowed", WARN, 0, 0, "/var/log/myrpc.log");
    }
    free(req_copy);
}

int main(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    Config cfg = parse_config("/etc/myRPC/myRPC.conf");
    int port = cfg.port;
    int is_stream = strcmp(cfg.socket_type, "stream") == 0;

    mysyslog("Server starting...", INFO, 0, 0, "/var/log/myrpc.log");

    int sock = socket(AF_INET, is_stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sock < 0) {
        mysyslog("Socket creation failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr, cli;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mysyslog("Bind failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("bind");
        close(sock);
        return 1;
    }

    if (is_stream) {
        listen(sock, 5);
        mysyslog("Listening (stream)", INFO, 0, 0, "/var/log/myrpc.log");
    } else {
        mysyslog("Listening (datagram)", INFO, 0, 0, "/var/log/myrpc.log");
    }

    while (!g_stop) {
        char buf[BUF_SZ] = {0};
        int n = 0;
        socklen_t clen = sizeof(cli);

        if (is_stream) {
            int csock = accept(sock, (struct sockaddr*)&cli, &clen);
            if (csock < 0) {
                mysyslog("Accept failed", ERROR, 0, 0, "/var/log/myrpc.log");
                perror("accept");
                continue;
            }
            n = recv(csock, buf, BUF_SZ - 1, 0);
            if (n <= 0) {
                close(csock);
                continue;
            }
            buf[n] = 0;
            mysyslog("Received request", INFO, 0, 0, "/var/log/myrpc.log");

            char resp[BUF_SZ];
            handle_client(buf, resp, sizeof(resp));
            send(csock, resp, strlen(resp), 0);
            close(csock);
        } else {
            n = recvfrom(sock, buf, BUF_SZ - 1, 0, (struct sockaddr*)&cli, &clen);
            if (n <= 0) continue;
            buf[n] = 0;
            mysyslog("Received request", INFO, 0, 0, "/var/log/myrpc.log");

            char resp[BUF_SZ];
            handle_client(buf, resp, sizeof(resp));
            sendto(sock, resp, strlen(resp), 0, (struct sockaddr*)&cli, clen);
        }
    }

    close(sock);
    mysyslog("Server stopped", INFO, 0, 0, "/var/log/myrpc.log");
    return 0;
}