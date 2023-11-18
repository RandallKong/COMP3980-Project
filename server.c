// Randall Kong
// A01279243

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void setup_signal_handler(void);
static void sigint_handler(int signum);
static void parse_arguments(int argc, char *argv[], char **ip_address,
                            char **port);
static void handle_arguments(const char *ip_address, const char *port_str,
                             in_port_t *port);
static in_port_t parse_in_port_t(const char *port_str);
static void convert_address(const char *address, struct sockaddr_storage *addr);
static int socket_create(int domain, int type, int protocol);
static void socket_bind(int sockfd, struct sockaddr_storage *addr,
                        in_port_t port);
static void start_listening(int server_fd, int backlog);
static int socket_accept_connection(int server_fd,
                                    struct sockaddr_storage *client_addr,
                                    socklen_t *client_addr_len);
static void handle_connection(int client_sockfd,
                              struct sockaddr_storage *client_addr);
static void socket_close(int sockfd);

void execute_command(char *arg);
pid_t create_child_process(void);
void await_child_process(void);
void parse_command_args(char *arg, char *args[]);
char *trim(const char *str);
void attemptCommand(char *path, char *args[]);

#define BASE_TEN 10
#define BUFFER 5000
#define ARGS_LEN 100

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[]) {
  char *address;
  char *port_str;
  in_port_t port;
  int backlog = BASE_TEN;
  int sockfd;
  struct sockaddr_storage addr;

  address = NULL;
  port_str = NULL;

  parse_arguments(argc, argv, &address, &port_str);
  handle_arguments(address, port_str, &port);
  convert_address(address, &addr);
  sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
  socket_bind(sockfd, &addr, port);
  start_listening(sockfd, backlog);
  setup_signal_handler();

  while (!exit_flag) {
    int client_sockfd;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof(client_addr);
    client_sockfd =
        socket_accept_connection(sockfd, &client_addr, &client_addr_len);

    if (client_sockfd == -1) {
      if (exit_flag) {
        break;
      }

      continue;
    }

    handle_connection(client_sockfd, &client_addr);
    socket_close(client_sockfd);
  }

  shutdown(sockfd, SHUT_RDWR);

  socket_close(sockfd);

  return EXIT_SUCCESS;
}

static void parse_arguments(int argc, char *argv[], char **ip_address,
                            char **port) {
  if (argc == 3) {
    *ip_address = argv[1];
    *port = argv[2];
  } else {
    printf("invalid num args\n");
    printf("usage: ./server [ip addr] [port]\n");
    exit(EXIT_FAILURE);
  }
}

static void handle_arguments(const char *ip_address, const char *port_str,
                             in_port_t *port) {
  if (ip_address == NULL) {
    printf("ip is null\n");
    exit(EXIT_FAILURE);
  }

  if (port_str == NULL) {
    printf("port str is null\n");
    exit(EXIT_FAILURE);
  }

  *port = parse_in_port_t(port_str);
}

in_port_t parse_in_port_t(const char *str) {
  char *endptr;
  uintmax_t parsed_value;

  errno = 0;
  parsed_value = strtoumax(str, &endptr, BASE_TEN);

  if (errno != 0) {
    perror("Error parsing in_port_t\n");
    exit(EXIT_FAILURE);
  }

  if (*endptr != '\0') {
    printf("non-numerics inside port\n");
    exit(EXIT_FAILURE);
  }

  if (parsed_value > UINT16_MAX) {
    printf("port out of range\n");
    exit(EXIT_FAILURE);
  }

  return (in_port_t)parsed_value;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void sigint_handler(int signum) { exit_flag = 1; }

#pragma GCC diagnostic pop

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
  int opt = 1;

  sockfd = socket(domain, type, protocol);

  if (sockfd == -1) {
    perror("Socket creation failed\n");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt\n");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  return sockfd;
}

static void socket_bind(int sockfd, struct sockaddr_storage *addr,
                        in_port_t port) {
  char addr_str[INET6_ADDRSTRLEN];
  socklen_t addr_len;
  void *vaddr;
  in_port_t net_port;

  net_port = htons(port);

  if (addr->ss_family == AF_INET) {
    struct sockaddr_in *ipv4_addr;

    ipv4_addr = (struct sockaddr_in *)addr;
    addr_len = sizeof(*ipv4_addr);
    ipv4_addr->sin_port = net_port;
    vaddr = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
  } else if (addr->ss_family == AF_INET6) {
    struct sockaddr_in6 *ipv6_addr;

    ipv6_addr = (struct sockaddr_in6 *)addr;
    addr_len = sizeof(*ipv6_addr);
    ipv6_addr->sin6_port = net_port;
    vaddr = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
  } else {
    fprintf(stderr,
            "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: "
            "%d\n",
            addr->ss_family);
    exit(EXIT_FAILURE);
  }

  if (inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL) {
    perror("inet_ntop\n");
    exit(EXIT_FAILURE);
  }

  printf("Binding to: %s:%u\n", addr_str, port);

  if (bind(sockfd, (struct sockaddr *)addr, addr_len) == -1) {
    perror("Binding failed");
    fprintf(stderr, "Error code: %d\n", errno);
    exit(EXIT_FAILURE);
  }

  printf("Bound to socket: %s:%u\n", addr_str, port);
}

static void start_listening(int server_fd, int backlog) {
  if (listen(server_fd, backlog) == -1) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Listening for incoming connections...\n");
}

static int socket_accept_connection(int server_fd,
                                    struct sockaddr_storage *client_addr,
                                    socklen_t *client_addr_len) {
  int client_fd;
  char client_host[NI_MAXHOST];
  char client_service[NI_MAXSERV];

  errno = 0;
  client_fd =
      accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

  if (client_fd == -1) {
    if (errno != EINTR) {
      perror("accept failed\n");
    }

    return -1;
  }

  if (getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host,
                  NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0) {
    printf("Accepted a new connection from %s:%s\n", client_host,
           client_service);
  } else {
    printf("Unable to get client information\n");
  }

  return client_fd;
}

static void setup_signal_handler(void) {
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
  sa.sa_handler = sigint_handler;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int client_sockfd,
                              struct sockaddr_storage *client_addr) {
  ssize_t bytesRead;
  char message[BUFFER];
  int client_stdout;
  int original_stdout;

  bytesRead = read(client_sockfd, message, sizeof(message));
  if (bytesRead > 0) {
    message[bytesRead] = '\0';
    printf("Command from client: %s\n", message);
  }

  // NOLINTNEXTLINE(android-cloexec-dup)
  original_stdout = dup(STDOUT_FILENO);
  if (original_stdout == -1) {
    perror("dup");
    exit(EXIT_FAILURE);
  }

  client_stdout = dup2(client_sockfd, STDOUT_FILENO);
  if (client_stdout == -1) {
    perror("dup2\n");
    exit(EXIT_FAILURE);
  }

  execute_command(message);
  fflush(stdout);

  // write(client_sockfd, "end", strlen("end"));

  if (dup2(original_stdout, STDOUT_FILENO) == -1) {
    perror("dup2 revert\n");
    exit(EXIT_FAILURE);
  }

  close(original_stdout);
  fflush(stdout);
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket\n");
    exit(EXIT_FAILURE);
  }
}

void execute_command(char *arg) {
  pid_t childPid;
  char *path = getenv("PATH");
  char *args[ARGS_LEN];

  for (int i = 0; i < ARGS_LEN - 1; i++) {
    args[i] = (char *)malloc(BASE_TEN * sizeof(char));
  }

  parse_command_args(arg, args);

  childPid = create_child_process();

  if (childPid == 0) {
    attemptCommand(path, args);
    for (int i = 0; i < ARGS_LEN - 1; i++) {
      free(args[i]);
    }
    exit(0);
  } else {
    await_child_process();
  }
}

pid_t create_child_process(void) {
  pid_t childPid = fork();

  if (childPid == -1) {
    perror("Error creating child process\n");
    return EXIT_FAILURE;
  }

  return childPid;
}

void await_child_process(void) {
  int child;

  wait(&child);
}

void parse_command_args(char *arg, char *args[]) {
  char *token;
  char *command;
  char *savePtr;
  int counter = 0;

  token = strtok_r(arg, " ", &savePtr);

  command = trim(token);

  args[counter] = command;

  while (token != NULL && counter < ARGS_LEN - 1) {
    char *option;
    counter++;
    token = strtok_r(NULL, " ", &savePtr);
    option = trim(token);
    args[counter] = option;
  }
}

char *trim(const char *str) {
  size_t start;
  size_t end;
  size_t len;
  char *trimmed;

  if (str == NULL) {
    return NULL;
  }

  start = 0;
  end = strlen(str) - 1;

  while (str[start] == ' ') {
    start++;
  }

  while (end > start && str[end] == ' ') {
    end--;
  }

  len = end - start + 1;

  trimmed = (char *)malloc(len + 1);

  if (trimmed == NULL) {
    return NULL;
  }

  strncpy(trimmed, str + start, len);
  trimmed[len] = '\0';

  return trimmed;
}

void attemptCommand(char *path, char *args[]) {
  char *saveptr;
  char *dir = strtok_r(path, ":", &saveptr);

  while (dir != NULL) {
    char testPath[PATH_MAX] = "";

    if (args == NULL) {
      perror("args are null\n");
      exit(EXIT_FAILURE);
    }

    snprintf(testPath, sizeof(testPath), "%s/%s", dir, args[0]);

    if (access(testPath, X_OK) == 0) {
      execv(testPath, args);
      perror("execv\n");
    }

    dir = strtok_r(NULL, ":", &saveptr);
  }

  printf("Invalid command.\n");
}
