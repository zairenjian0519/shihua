#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET diag_socket_t;
#define DIAG_INVALID_SOCKET INVALID_SOCKET
#define diag_close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int diag_socket_t;
#define DIAG_INVALID_SOCKET (-1)
#define diag_close_socket close
#endif

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 24040

static int socket_init_once(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return 1;
#endif
}

static void socket_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static void print_usage(void)
{
    printf("Usage:\n");
    printf("  iec104-diag [--host 127.0.0.1] [--port 24040] <command> [args]\n");
    printf("\nCommands:\n");
    printf("  status\n");
    printf("  get --ioa <ioa>\n");
    printf("  set-yx --ioa <ioa> --value <0|1> [--quality <0..255>]\n");
    printf("  set-yc --ioa <ioa> --value <float> [--quality <0..255>]\n");
    printf("  set-dd --ioa <ioa> --value <int> [--quality <0..255>]\n");
    printf("  soe list | soe add --ioa <ioa> --value <0|1> [--quality <0..255>] | soe clear\n");
    printf("  active-upload list [--type soe] | active-upload notify | active-upload clear\n");
}

static void append_arg(char* command, size_t command_size, const char* arg)
{
    if (command[0] != '\0')
        strncat(command, " ", command_size - strlen(command) - 1);
    strncat(command, arg, command_size - strlen(command) - 1);
}

int main(int argc, char** argv)
{
    const char* host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    char command[1024] = {0};
    char response[4096] = {0};
    diag_socket_t fd;
    struct sockaddr_in addr;
    int command_started = 0;
    int received;

    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
            continue;
        }

        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            continue;
        }

        command_started = 1;
        append_arg(command, sizeof(command), argv[i]);
    }

    if (!command_started || command[0] == '\0') {
        print_usage();
        return EXIT_FAILURE;
    }

    strncat(command, "\n", sizeof(command) - strlen(command) - 1);

    if (!socket_init_once()) {
        fprintf(stderr, "socket init failed\n");
        return EXIT_FAILURE;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == DIAG_INVALID_SOCKET) {
        fprintf(stderr, "socket create failed\n");
        socket_cleanup();
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "invalid host: %s\n", host);
        diag_close_socket(fd);
        socket_cleanup();
        return EXIT_FAILURE;
    }

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "connect failed: %s:%d\n", host, port);
        diag_close_socket(fd);
        socket_cleanup();
        return EXIT_FAILURE;
    }

    send(fd, command, (int)strlen(command), 0);
    received = (int)recv(fd, response, sizeof(response) - 1, 0);
    if (received > 0) {
        response[received] = '\0';
        printf("%s", response);
    }
    else {
        fprintf(stderr, "no response\n");
    }

    diag_close_socket(fd);
    socket_cleanup();
    return received > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
