// Randall Kong
// A01279243

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void parse_arguments(int argc, char *argv[], char **ip_address,
                            char **port, char **command);
static void handle_arguments(const char *ip_address, const char *port_str,
                             in_port_t *port);
static in_port_t parse_in_port_t(const char *port_str);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int socket_create(int domain, int type, int protocol);
static void socket_connect(int sockfd, struct sockaddr_storage *addr,
                           in_port_t port);
static void socket_close(int client_fd);

#define BASE_TEN 10
#define MESSAGE_SIZE 5000

int main(int argc, char *argv[]) {
  char *address;
  char *port_str;
  char *command;
  in_port_t port;
  int sockfd;
  struct sockaddr_storage addr;
  ssize_t bytesRead;
  char message[MESSAGE_SIZE];

  address = NULL;
  port_str = NULL;
  command = NULL;
  parse_arguments(argc, argv, &address, &port_str, &command);
  handle_arguments(address, port_str, &port);
  convert_address(address, &addr);
  sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
  socket_connect(sockfd, &addr, port);

  write(sockfd, command, strlen(command));

  bytesRead = read(sockfd, message, sizeof(message));

  if (bytesRead > 0) {
    message[bytesRead] = '\0';

    printf("%s", message);
  }

  shutdown(sockfd, SHUT_RDWR);

  socket_close(sockfd);

  return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address,
                            char **port, char **command) {
  if (argc == 4) {
    *ip_address = argv[1];
    *port = argv[2];
    *command = argv[3];
  } else {
    printf("invalid num args\n");
    printf("usage: ./client [server ip addr] [port] [command]\n");
    exit(EXIT_FAILURE);
  }
}

static void handle_arguments(const char *ip_address, const char *port_str,
                             in_port_t *port) {
  if (ip_address == NULL) {
    printf("ip address required\n");
    exit(EXIT_FAILURE);
  }

  if (port_str == NULL) {
    printf("port required\n");
    exit(EXIT_FAILURE);
  }

  *port = parse_in_port_t(port_str);
}

static in_port_t parse_in_port_t(const char *str) {
  char *endptr;
  uintmax_t parsed_value;

  errno = 0;
  parsed_value = strtoumax(str, &endptr, BASE_TEN);

  if (errno != 0) {
    perror("Error parsing in_port_t\n");
    exit(EXIT_FAILURE);
  }

  // Check if there are any non-numeric characters in the input string
  if (*endptr != '\0') {
    printf("invalid chars in port\n");
    exit(EXIT_FAILURE);
  }

  // Check if the parsed value is within the valid range for in_port_t
  if (parsed_value > UINT16_MAX) {
    printf("port value out of range\n");
    exit(EXIT_FAILURE);
  }

  return (in_port_t)parsed_value;
}

static void convert_address(const char *address,
                            struct sockaddr_storage *addr) {
  memset(addr, 0, sizeof(*addr));

  if (inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) ==
      1) {
    addr->ss_family = AF_INET;
  } else if (inet_pton(AF_INET6, address,
                       &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1) {
    addr->ss_family = AF_INET6;
  } else {
    fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
    exit(EXIT_FAILURE);
  }
}

static int socket_create(int domain, int type, int protocol) {
  int sockfd;

  sockfd = socket(domain, type, protocol);

  if (sockfd == -1) {
    perror("Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_connect(int sockfd, struct sockaddr_storage *addr,
                           in_port_t port) {
  char addr_str[INET6_ADDRSTRLEN];
  in_port_t net_port;
  socklen_t addr_len;

  if (inet_ntop(addr->ss_family,
                addr->ss_family == AF_INET
                    ? (void *)&(((struct sockaddr_in *)addr)->sin_addr)
                    : (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr),
                addr_str, sizeof(addr_str)) == NULL) {
    perror("inet_ntop\n");
    exit(EXIT_FAILURE);
  }

  printf("Connecting to: %s:%u\n", addr_str, port);
  net_port = htons(port);

  if (addr->ss_family == AF_INET) {
    struct sockaddr_in *ipv4_addr;

    ipv4_addr = (struct sockaddr_in *)addr;
    ipv4_addr->sin_port = net_port;
    addr_len = sizeof(struct sockaddr_in);
  } else if (addr->ss_family == AF_INET6) {
    struct sockaddr_in6 *ipv6_addr;

    ipv6_addr = (struct sockaddr_in6 *)addr;
    ipv6_addr->sin6_port = net_port;
    addr_len = sizeof(struct sockaddr_in6);
  } else {
    fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
    exit(EXIT_FAILURE);
  }

  if (connect(sockfd, (struct sockaddr *)addr, addr_len) == -1) {
    printf("there are no existing network server sockets available with this "
           "ip and port\n");
    exit(EXIT_FAILURE);
  }

  printf("Connected to: %s:%u\n", addr_str, port);
}

static void socket_close(int client_fd) {
  if (close(client_fd) == -1) {
    perror("Error closing socket\n");
    exit(EXIT_FAILURE);
  }
}
