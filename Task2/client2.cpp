#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

constexpr int PORT1 = 4001;
constexpr int PORT2 = 4002;
constexpr int PORT3 = 4003;
constexpr int CONTROL_PORT = 4000;
constexpr int TIMEOUT_MS = 20;
constexpr int BUFFER_SIZE = 1024;

// Define control server packets with packed attribute
struct control_server_read_packet {
  uint16_t op_code;
  uint16_t object;
  uint16_t property;
} __attribute__((packed));

struct control_server_write_packet {
  uint16_t op_code;
  uint16_t object;
  uint16_t property;
  uint16_t value;
} __attribute__((packed));

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

void control_channel_worker(int cntrl_sockfd, double op3,
                            const struct sockaddr_in &serv_addr) {
  control_server_write_packet write_freq, write_amplitude;

  write_freq.op_code = 0x0200U;
  write_freq.object = 0x0100U;
  write_freq.property = htons(255);

  write_amplitude.op_code = 0x0200U;
  write_amplitude.object = 0x0100U;
  write_amplitude.property = htons(170);

  if (op3 >= 3.0) {
    write_freq.value = htons(1000);
    write_amplitude.value = htons(8000);

  } else {
    write_freq.value = htons(2000);
    write_amplitude.value = htons(4000);
  }

  sendto(cntrl_sockfd, &write_freq, sizeof(write_freq), MSG_CONFIRM,
         (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
  sendto(cntrl_sockfd, &write_amplitude, sizeof(write_amplitude), MSG_CONFIRM,
         (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
}

void create_tcp_sockets(int &sock1, int &sock2, int &sock3) {
  if ((sock1 = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
      (sock2 = socket(AF_INET, SOCK_STREAM, 0)) < 0 ||
      (sock3 = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket creation failed");
    exit(EXIT_FAILURE);
  }
}

void setup_control_channel(int &ctrl_sockfd, sockaddr_in &ctrl_addr) {
  if ((ctrl_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("Socket creation error");
    exit(EXIT_FAILURE);
  }

  memset(&ctrl_addr, 0, sizeof(ctrl_addr));
  ctrl_addr.sin_family = AF_INET;
  ctrl_addr.sin_port = htons(CONTROL_PORT);
  if (inet_pton(AF_INET, "127.0.0.1", &ctrl_addr.sin_addr) <= 0) {
    perror("Invalid address/Address not supported");
    exit(EXIT_FAILURE);
  }
}

void setup_tcp_addr(sockaddr_in &addr) {
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
}

void connect_to_ports(int sock1, int sock2, int sock3,
                      const sockaddr_in &addr) {
  for (int port : {PORT1, PORT2, PORT3}) {
    sockaddr_in addr_copy = addr;
    addr_copy.sin_port = htons(port);
    int sock = (port == PORT1) ? sock1 : (port == PORT2) ? sock2 : sock3;
    if (connect(sock, (struct sockaddr *)&addr_copy, sizeof(addr_copy)) < 0) {
      perror("connect failed");
      exit(EXIT_FAILURE);
    }
  }
}

int create_timer() {
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (timer_fd == -1) {
    perror("timerfd_create failed");
    exit(EXIT_FAILURE);
  }

  struct itimerspec timer_spec;
  timer_spec.it_value.tv_sec = 0;
  timer_spec.it_value.tv_nsec = TIMEOUT_MS * 1000000;
  timer_spec.it_interval.tv_sec = 0;
  timer_spec.it_interval.tv_nsec = TIMEOUT_MS * 1000000;

  if (timerfd_settime(timer_fd, 0, &timer_spec, nullptr) == -1) {
    perror("timerfd_settime failed");
    exit(EXIT_FAILURE);
  }

  return timer_fd;
}

void setup_fd_set(fd_set &readfds, int tcp_sock1, int tcp_sock2, int tcp_sock3,
                  int timer_fd, int &max_fd) {
  FD_ZERO(&readfds);
  FD_SET(tcp_sock1, &readfds);
  FD_SET(tcp_sock2, &readfds);
  FD_SET(tcp_sock3, &readfds);
  FD_SET(timer_fd, &readfds);
  max_fd = std::max({tcp_sock1, tcp_sock2, tcp_sock3, timer_fd}) + 1;
}

void process_tcp_socket(int tcp_sock, std::string &out) {
  char buffer[BUFFER_SIZE] = {0};
  int len = read(tcp_sock, buffer, sizeof(buffer) - 1);
  if (len > 0) {
    std::string data(buffer, len);
    out = extract_latest_value(data);
  }
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

void handle_timer_event(int timer_fd, std::string &out1, std::string &out2,
                        std::string &out3, int tcp_sock1, int tcp_sock2,
                        int tcp_sock3, int &cntrl_sockfd,
                        sockaddr_in &udp_cntrl_srvr_addr, fd_set &readfds) {
  uint64_t expirations;
  read(timer_fd, &expirations, sizeof(expirations)); // Clear the timer event

  std::stringstream timestamp_stream;
  timestamp_stream << std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

  if (FD_ISSET(tcp_sock1, &readfds)) {
    process_tcp_socket(tcp_sock1, out1);
  }

  if (FD_ISSET(tcp_sock2, &readfds)) {
    process_tcp_socket(tcp_sock2, out2);
  }

  if (FD_ISSET(tcp_sock3, &readfds)) {
    process_tcp_socket(tcp_sock3, out3);
    control_channel_worker(cntrl_sockfd, std::stod(out3), udp_cntrl_srvr_addr);
  }

  print_json_output(timestamp_stream.str(), out1, out2, out3);
}

void handle_select(int tcp_sock1, int tcp_sock2, int tcp_sock3, int timer_fd,
                   int &cntrl_sockfd, sockaddr_in &udp_cntrl_srvr_addr) {
  fd_set readfds;
  int max_fd;
  std::string out1 = "--", out2 = "--", out3 = "--";

  while (true) {
    setup_fd_set(readfds, tcp_sock1, tcp_sock2, tcp_sock3, timer_fd, max_fd);

    struct timeval select_timeout;
    select_timeout.tv_sec = 0;
    select_timeout.tv_usec = TIMEOUT_MS * 1000;

    fd_set temp_fds = readfds; // Create a copy of readfds
    int activity = select(max_fd, &temp_fds, nullptr, nullptr, &select_timeout);
    if (activity < 0) {
      perror("select error");
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(timer_fd, &temp_fds)) {
      handle_timer_event(timer_fd, out1, out2, out3, tcp_sock1, tcp_sock2,
                         tcp_sock3, cntrl_sockfd, udp_cntrl_srvr_addr,
                         temp_fds);
    }
  }
}

int main() {
  int tcp_sock1, tcp_sock2, tcp_sock3, cntrl_sockfd;
  sockaddr_in tcp_srvr_addr, udp_cntrl_srvr_addr;

  create_tcp_sockets(tcp_sock1, tcp_sock2, tcp_sock3);
  setup_control_channel(cntrl_sockfd, udp_cntrl_srvr_addr);
  setup_tcp_addr(tcp_srvr_addr);
  connect_to_ports(tcp_sock1, tcp_sock2, tcp_sock3, tcp_srvr_addr);

  int timer_fd = create_timer();
  handle_select(tcp_sock1, tcp_sock2, tcp_sock3, timer_fd, cntrl_sockfd,
                udp_cntrl_srvr_addr);

  return 0;
}
