#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
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

static int socket_bind(int sockfd, struct sockaddr_storage *addr,
                       in_port_t port);

static void start_listening(int server_fd);

static int socket_accept_connection(int server_fd,
                                    struct sockaddr_storage *client_addr,
                                    socklen_t *client_addr_len);

static void socket_connect(int sockfd, struct sockaddr_storage *addr,
                           in_port_t port);

static void handle_connection(int client_sockfd,
                              struct sockaddr_storage *client_addr);

static void *read_thread_function(void *arg);

static void *write_thread_function(void *arg);

static void socket_close(int sockfd);

bool isStdinReady(void);

static void *file_thread(void *arg);

#define BASE_TEN 10
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 1

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[]) {
  char *address;
  char *port_str;
  in_port_t port;
  int sockfd;
  struct sockaddr_storage addr;
  int bindResult;

  address = NULL;
  port_str = NULL;

  parse_arguments(argc, argv, &address, &port_str);
  handle_arguments(address, port_str, &port);
  convert_address(address, &addr);
  sockfd = socket_create(addr.ss_family, SOCK_STREAM, 0);
  bindResult = socket_bind(sockfd, &addr, port);

  setup_signal_handler();

  if (bindResult != -1) {
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    start_listening(sockfd);

    while (!exit_flag) {
      int client_sockfd =
          socket_accept_connection(sockfd, &client_addr, &client_addr_len);
      if (client_sockfd != -1) {
        handle_connection(client_sockfd, &client_addr);
        socket_close(client_sockfd);
      }
    }
  } else {
    socket_connect(sockfd, &addr, port);
    handle_connection(sockfd, &addr);
  }

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
    printf("usage: ./chat [ip addr] [port]\n");
    printf("usage: ./chat [ip addr] [port] < [.txt file]\n");
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

static int socket_bind(int sockfd, struct sockaddr_storage *addr,
                       in_port_t port) {
  char addr_str[INET6_ADDRSTRLEN];
  socklen_t addr_len;
  void *vaddr;
  in_port_t net_port;
  int bindResult;

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

  bindResult = bind(sockfd, (struct sockaddr *)addr, addr_len);

  if (bindResult != -1) {
    printf("Bound to socket: %s:%u\n", addr_str, port);
  }

  return bindResult;
}

static void start_listening(int server_fd) {
  if (listen(server_fd, MAX_CONNECTIONS) == -1) {
    perror("listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Waiting for a friend to connect...\n");
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
    printf("You are now chatting with %s:%s\n", client_host, client_service);
  } else {
    printf("Unable to get client information\n");
  }

  return client_fd;
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

  //  printf("Connecting to: %s:%u\n", addr_str, port);
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
    printf("Port out of range or non existent ip addr\n");
    exit(EXIT_FAILURE);
  }

  printf("Connecting to: %s:%u\n", addr_str, port);
  printf("You are now chatting with the host of %s:%u\n", addr_str, port);
}

static void setup_signal_handler(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void handle_connection(int sockfd,
                              struct sockaddr_storage *client_addr) {
  pthread_t read_thread;
  pthread_t write_thread;
  pthread_t stdin_thread = {0};
  bool stdin_thread_created = false;

  if (isStdinReady()) {
    if (pthread_create(&stdin_thread, NULL, file_thread, &sockfd) == 0) {
      stdin_thread_created = true;
    } else {
      perror("Failed to create stdin thread");
      exit(EXIT_FAILURE);
    }
  }

  if (pthread_create(&read_thread, NULL, read_thread_function, &sockfd) != 0) {
    perror("Failed to create read thread");
    exit(EXIT_FAILURE);
  }

  if (pthread_create(&write_thread, NULL, write_thread_function, &sockfd) !=
      0) {
    perror("Failed to create write thread");
    exit(EXIT_FAILURE);
  }

  if (stdin_thread_created) {
    pthread_join(stdin_thread, NULL);
  }

  pthread_join(read_thread, NULL);
  pthread_join(write_thread, NULL);
}

static void *read_thread_function(void *arg) {
  int sockfd = *((int *)arg);
  char buffer[BUFFER_SIZE];
  int tty_fd;

  while (!exit_flag) {
    int is_stdin_terminal;
    FILE *input_stream;
    // Check if STDIN_FILENO is a terminal in each iteration
    is_stdin_terminal = isatty(STDIN_FILENO);

    if (!is_stdin_terminal) {
      tty_fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);
      if (tty_fd == -1) {
        perror("Unable to open /dev/tty");
        continue;
      }
    } else {
      tty_fd = STDIN_FILENO;
    }

    input_stream = is_stdin_terminal ? stdin : fdopen(tty_fd, "r");
    if (fgets(buffer, BUFFER_SIZE, input_stream) != NULL) {
      send(sockfd, buffer, strlen(buffer), 0);
    } else {
      exit_flag = 1;
      shutdown(sockfd, SHUT_RDWR);
      break;
    }

    if (!is_stdin_terminal) {
      fclose(input_stream);
    }
  }

  return NULL;
}

static void *write_thread_function(void *arg) {
  int sockfd = *((int *)arg);
  char buffer[BUFFER_SIZE];

  while (!exit_flag) {
    ssize_t n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
    if (n <= 0) {
      exit_flag = 1;
      break;
    }
    buffer[n] = '\0';
    printf("Received:%s", buffer);
  }

  return NULL;
}

bool isStdinReady(void) {
  fd_set read_fds;
  struct timeval tv;
  int retval;

  //  // Set up the timeout. Here, we're setting it to 5 seconds.
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  // Initialize the set of file descriptors.
  FD_ZERO(&read_fds);
  FD_SET(STDIN_FILENO, &read_fds);

  // Wait for input on stdin (file descriptor 0).
  retval = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &tv);

  if (retval == -1) {
    perror("select()");
    return false;
  }

  if (retval) {
    return true; // Data is available
  }

  return false; // No data within the timeout period or an error occurred
}

static void *file_thread(void *arg) {
  int sockfd = *((int *)arg);
  char buffer[BUFFER_SIZE];

  ssize_t n;

  n = read(STDIN_FILENO, buffer, BUFFER_SIZE - 1);
  if (n > 0) {
    buffer[n] = '\n';
    send(sockfd, buffer, strlen(buffer), 0);
  } else if (n == 0) {
    printf("No contents in file");
  } else if (errno != EINTR) {
    perror("read error");
    exit(EXIT_FAILURE);
  }

  return NULL;
}

#pragma GCC diagnostic pop

static void socket_close(int sockfd) {
  if (close(sockfd) == -1) {
    perror("Error closing socket\n");
    exit(EXIT_FAILURE);
  }
}
