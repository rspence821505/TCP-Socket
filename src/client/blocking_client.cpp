#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "common.hpp"

struct Connection {
  int sockfd;
  int port;
  std::string buffer;
  uint64_t message_count;

  Connection(int fd, int p) : sockfd(fd), port(p), message_count(0) {}
};

void process_message(const std::string &message, Connection &conn) {
  std::stringstream ss(message);
  std::string timestamp, symbol, price_str, volume_str;

  std::getline(ss, timestamp, ',');
  std::getline(ss, symbol, ',');
  std::getline(ss, price_str, ',');
  std::getline(ss, volume_str, ',');

  // Show which exchange this came from
  std::cout << "[Exchange " << conn.port << "] [" << symbol << "] $"
            << price_str << " @ " << volume_str << std::endl;

  conn.message_count++;
}

int connect_to_exchange(int port) {
  SocketOptions opts;
  opts.non_blocking = true;

  auto result = socket_connect("127.0.0.1", port, opts);
  if (!result) {
    LOG_ERROR("Client", "%s", result.error().c_str());
    return -1;
  }

  std::cout << "Connected to exchange on port " << port << std::endl;
  return result.value();
}

bool drain_socket(Connection &conn) { // Changed from void to bool
  while (true) {
    char temp[4096];
    ssize_t bytes_read = recv(conn.sockfd, temp, sizeof(temp), 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break; // No more data
      } else {
        LOG_PERROR("Client", "recv failed");
        return false; // Connection error
      }
    } else if (bytes_read == 0) {
      LOG_INFO("Client", "Exchange %d closed connection", conn.port);
      return false; // Connection closed
    } else {
      conn.buffer.append(temp, bytes_read);

      size_t pos;
      while ((pos = conn.buffer.find('\n')) != std::string::npos) {
        std::string message = conn.buffer.substr(0, pos);
        process_message(message, conn);
        conn.buffer.erase(0, pos + 1);
      }
    }
  }
  return true; // Connection still alive
}

int main() {
  // Create kqueue
  int kq = kqueue();
  if (kq == -1) {
    LOG_PERROR("Client", "kqueue creation failed");
    return 1;
  }

  // Connect to 3 exchanges
  std::vector<int> ports = {9999, 10000, 10001};
  std::map<int, Connection> connections; // Map fd -> Connection

  for (int port : ports) {
    int sockfd = connect_to_exchange(port);
    if (sockfd < 0) {
      continue; // Skip failed connections
    }

    // Register with kqueue
    struct kevent ev_set;
    EV_SET(&ev_set, sockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kq, &ev_set, 1, NULL, 0, NULL) == -1) {
      LOG_PERROR("Client", "kevent registration failed");
      close(sockfd);
      continue;
    }

    // Store connection
    connections.emplace(sockfd, Connection(sockfd, port));
  }

  if (connections.empty()) {
    LOG_ERROR("Client", "Failed to connect to any exchanges");
    return 1;
  }

  std::cout << "Multiplexing " << connections.size() << " exchanges"
            << std::endl;

  auto start_time = std::chrono::steady_clock::now();
  int active_connections = connections.size();

  // Event loop
  while (active_connections > 0) {
    struct kevent ev_list[10];

    int nev = kevent(kq, NULL, 0, ev_list, 10, NULL);
    if (nev == -1) {
      LOG_PERROR("Client", "kevent wait failed");
      break;
    }

    // Process each ready socket
    for (int i = 0; i < nev; i++) {
      int ready_fd = ev_list[i].ident;

      // Find the connection
      auto it = connections.find(ready_fd);
      if (it == connections.end()) {
        continue; // Shouldn't happen
      }

      Connection &conn = it->second;

      // Read from this connection
      bool still_alive = drain_socket(conn);

      if (!still_alive) {
        // Connection closed
        std::cout << "Removing connection to exchange " << conn.port
                  << std::endl;
        close(conn.sockfd);
        connections.erase(it);
        active_connections--;
        std::cout << "Active connections remaining: " << active_connections
                  << std::endl;
      }
    }
  }

  // Calculate statistics
  auto end_time = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  double seconds = duration.count() / 1000.0;

  std::cout << "\n=== Statistics ===" << std::endl;
  uint64_t total_messages = 0;
  for (const auto &[fd, conn] : connections) {
    std::cout << "Exchange " << conn.port << ": " << conn.message_count
              << " messages" << std::endl;
    total_messages += conn.message_count;
  }

  std::cout << "Total: " << total_messages << " messages in " << seconds
            << "s (" << static_cast<int>(total_messages / seconds)
            << " msgs/sec)" << std::endl;

  // Cleanup
  for (const auto &[fd, conn] : connections) {
    close(fd);
  }
  close(kq);

  return 0;
}