#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "libmysyslog.h"

#define BUF_SZ 1024

static void usage() {
    puts("Usage: myRPC-client [OPTIONS]");
    puts("Options:");
    puts("  -c, --command \"bash_command\"   Command to execute");
    puts("  -h, --host \"ip_addr\"          Server IP address");
    puts("  -p, --port PORT                Server port");
    puts("  -s, --stream                   Use stream socket");
    puts("  -d, --dgram                    Use datagram socket");
    puts("      --help                     Display this help and exit");
}

int main(int argc, char **argv) {
    char *cmd = NULL, *host = NULL;
    int port = 0, tcp = 1, ch, idx = 0;

    static struct option opts[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    while ((ch = getopt_long(argc, argv, "c:h:p:sd", opts, &idx)) != -1) {
        if (ch == 0) { usage(); return 0; }
        switch (ch) {
            case 'c': cmd = optarg; break;
            case 'h': host = optarg; break;
            case 'p': port = atoi(optarg); break;
            case 's': tcp = 1; break;
            case 'd': tcp = 0; break;
            default: usage(); return 1;
        }
    }

    if (!cmd || !host || !port) {
        fprintf(stderr, "Missing required arguments\n");
        usage();
        return 1;
    }

    struct passwd *pw = getpwuid(getuid());
    const char *user = pw ? pw->pw_name : "unknown";

    char req[BUF_SZ];
    snprintf(req, sizeof(req), "%s: %s", user, cmd);

    mysyslog("Connecting to server...", INFO, 0, 0, "/var/log/myrpc.log");

    int sock = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sock < 0) {
        mysyslog("Socket creation failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("socket");
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);
    inet_pton(AF_INET, host, &srv.sin_addr);

    char resp[BUF_SZ];

    if (tcp) {
        if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
            mysyslog("Connect failed", ERROR, 0, 0, "/var/log/myrpc.log");
            perror("connect");
            close(sock);
            return 1;
        }
        mysyslog("Connected", INFO, 0, 0, "/var/log/myrpc.log");
        send(sock, req, strlen(req), 0);
        int n = recv(sock, resp, sizeof(resp) - 1, 0);
        if (n > 0) resp[n] = 0; else resp[0] = 0;
        printf("Server response: %s\n", resp);
        mysyslog("Response received", INFO, 0, 0, "/var/log/myrpc.log");
    } else {
        sendto(sock, req, strlen(req), 0, (struct sockaddr*)&srv, sizeof(srv));
        socklen_t slen = sizeof(srv);
        int n = recvfrom(sock, resp, sizeof(resp) - 1, 0, (struct sockaddr*)&srv, &slen);
        if (n > 0) resp[n] = 0; else resp[0] = 0;
        printf("Server response: %s\n", resp);
        mysyslog("Response received", INFO, 0, 0, "/var/log/myrpc.log");
    }

    close(sock);
    return 0;
}