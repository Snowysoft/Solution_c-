#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

constexpr int PORT1 = 4001;
constexpr int PORT2 = 4002;
constexpr int PORT3 = 4003;
constexpr int TIMEOUT_MS = 100;
constexpr int BUFFER_SIZE = 1024;

std::string extract_latest_value(const std::string &data) {
  size_t last_newline = data.find_last_of('\n');
  if (last_newline != std::string::npos && last_newline > 0) {
    size_t second_last_newline = data.find_last_of('\n', last_newline - 1);
    if (second_last_newline != std::string::npos) {
      return data.substr(second_last_newline + 1,
                         last_newline - second_last_newline - 1);
    }
  }
  return data.substr(0, last_newline);
}

int create_socket(int domain, int type, int protocol) {
  int sockfd = socket(domain, type, protocol);
  if (sockfd < 0) {
    perror("socket creation failed");
    exit(1);
  }
  return sockfd;
}

void setup_server_address(sockaddr_in &addr, int port) {
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
}

void connect_to_ports(int tcp_sock1, int tcp_sock2, int tcp_sock3,
                      const sockaddr_in &addr) {
  for (int port : {PORT1, PORT2, PORT3}) {
    sockaddr_in addr_copy = addr;
    addr_copy.sin_port = htons(port);
    int sock = (port == PORT1)   ? tcp_sock1
               : (port == PORT2) ? tcp_sock2
                                 : tcp_sock3;
    if (connect(sock, (struct sockaddr *)&addr_copy, sizeof(addr_copy)) < 0) {
      perror("connect failed");
      exit(1);
    }
  }
}

int create_and_setup_timer() {
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (timer_fd == -1) {
    perror("timerfd_create failed");
    exit(1);
  }

  struct itimerspec timer_spec;
  timer_spec.it_value.tv_sec = 0;
  timer_spec.it_value.tv_nsec = TIMEOUT_MS * 1000000;
  timer_spec.it_interval.tv_sec = 0;
  timer_spec.it_interval.tv_nsec = TIMEOUT_MS * 1000000;

  if (timerfd_settime(timer_fd, 0, &timer_spec, nullptr) == -1) {
    perror("timerfd_settime failed");
    exit(1);
  }

  return timer_fd;
}

void print_json_output(const std::string &timestamp, const std::string &out1,
                       const std::string &out2, const std::string &out3) {
  std::cout << "{"
            << "\"timestamp\": " << timestamp << ", "
            << "\"out1\": \"" << out1 << "\", "
            << "\"out2\": \"" << out2 << "\", "
            << "\"out3\": \"" << out3 << "\""
            << "}" << std::endl;
}

void handle_timer_event(int timer_fd, int tcp_sock1, int tcp_sock2,
                        int tcp_sock3, fd_set &readfds) {
  uint64_t expirations;
  read(timer_fd, &expirations, sizeof(expirations)); // Clear the timer event

  std::stringstream timestamp_stream;
  timestamp_stream << std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

  char buffer[BUFFER_SIZE];
  std::string out1 = "--", out2 = "--", out3 = "--";

  if (FD_ISSET(tcp_sock1, &readfds)) {
    memset(buffer, 0, sizeof(buffer));
    int len = read(tcp_sock1, buffer, sizeof(buffer) - 1);
    if (len > 0) {
      std::string data(buffer, len);
      out1 = extract_latest_value(data);
    }
  }

  if (FD_ISSET(tcp_sock2, &readfds)) {
    memset(buffer, 0, sizeof(buffer));
    int len = read(tcp_sock2, buffer, sizeof(buffer) - 1);
    if (len > 0) {
      std::string data(buffer, len);
      out2 = extract_latest_value(data);
    }
  }

  if (FD_ISSET(tcp_sock3, &readfds)) {
    memset(buffer, 0, sizeof(buffer));
    int len = read(tcp_sock3, buffer, sizeof(buffer) - 1);
    if (len > 0) {
      std::string data(buffer, len);
      out3 = extract_latest_value(data);
    }
  }

  print_json_output(timestamp_stream.str(), out1, out2, out3);
}

void handle_selects(int tcp_sock1, int tcp_sock2, int tcp_sock3, int timer_fd) {
  fd_set readfds;
  char buffer[BUFFER_SIZE];

  while (true) {
    // Clear and set the file descriptor set
    FD_ZERO(&readfds);
    FD_SET(tcp_sock1, &readfds);
    FD_SET(tcp_sock2, &readfds);
    FD_SET(tcp_sock3, &readfds);
    FD_SET(timer_fd, &readfds);

    // Find the maximum file descriptor
    int max_fd = std::max({tcp_sock1, tcp_sock2, tcp_sock3, timer_fd}) + 1;

    // Set the timeout for select
    struct timeval select_timeout;
    select_timeout.tv_sec = 0;
    select_timeout.tv_usec = TIMEOUT_MS * 1000;

    // Wait for activity on the file descriptors
    int activity = select(max_fd, &readfds, nullptr, nullptr, &select_timeout);
    if (activity < 0) {
      perror("select error");
      exit(1);
    }

    // Handle timer event if it is set
    if (FD_ISSET(timer_fd, &readfds)) {
      handle_timer_event(timer_fd, tcp_sock1, tcp_sock2, tcp_sock3, readfds);
    }
  }
}

int main() {
  int tcp_sock1 = create_socket(AF_INET, SOCK_STREAM, 0);
  int tcp_sock2 = create_socket(AF_INET, SOCK_STREAM, 0);
  int tcp_sock3 = create_socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in tcp_srvr_addr;

  setup_server_address(tcp_srvr_addr, 0); // Port will be set later
  connect_to_ports(tcp_sock1, tcp_sock2, tcp_sock3, tcp_srvr_addr);

  int timer_fd = create_and_setup_timer();

  // Run the event loop
  handle_selects(tcp_sock1, tcp_sock2, tcp_sock3, timer_fd);

  return 0;
}
