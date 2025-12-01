#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

// Platform-specific includes
#ifdef __linux__
#include <sys/epoll.h>
#define USE_EPOLL
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#define USE_KQUEUE
#else
#error "Unsupported platform"
#endif

#include "binary_protocol.hpp"
#include "common.hpp"

struct Connection {
  int sockfd;
  int port;
  std::string buffer; // Accumulation buffer for partial messages
  uint64_t message_count;

  Connection(int fd, int p) : sockfd(fd), port(p), message_count(0) {}
};

void process_message(const BinaryTick &tick, Connection &conn) {
  // Convert symbol to string (handle null padding)
  std::string symbol(tick.symbol, 4);
  // Remove null padding for display
  size_t null_pos = symbol.find('\0');
  if (null_pos != std::string::npos) {
    symbol = symbol.substr(0, null_pos);
  }

  std::cout << "[Exchange " << conn.port << "] [" << symbol << "] $"
            << tick.price << " @ " << tick.volume << std::endl;

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

bool drain_socket(Connection &conn) {
  while (true) {
    // Step 1: Try to read more data from socket
    char temp[4096];
    ssize_t bytes_read = recv(conn.sockfd, temp, sizeof(temp), 0);

    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break; // No more data available right now
      } else {
        LOG_PERROR("Client", "recv failed");
        return false; // Connection error
      }
    } else if (bytes_read == 0) {
      LOG_INFO("Client", "Exchange %d closed connection", conn.port);
      return false; // Connection closed
    } else {
      // Append received bytes to accumulation buffer
      conn.buffer.append(temp, bytes_read);
    }

    // Step 2: Process all complete messages in the buffer
    while (true) {
      // Check if we have at least 4 bytes for the length prefix
      if (conn.buffer.size() < 4) {
        break; // Need more data for length prefix
      }

      // Read the length prefix (first 4 bytes)
      uint32_t length_net;
      memcpy(&length_net, conn.buffer.data(), 4);
      uint32_t length = ntohl(length_net);

      // Sanity check: length should be reasonable (20 bytes for our format)
      if (length != BinaryTick::PAYLOAD_SIZE) {
        LOG_ERROR("Client", "Invalid message length: %u (expected %zu)",
                  length, BinaryTick::PAYLOAD_SIZE);
        return false; // Protocol error
      }

      // Check if we have the complete message (length prefix + payload)
      size_t total_message_size = 4 + length;
      if (conn.buffer.size() < total_message_size) {
        break; // Need more data for complete message
      }

      // Extract and deserialize the payload
      const char *payload = conn.buffer.data() + 4; // Skip length prefix
      BinaryTick tick = deserialize_tick(payload);

      // Process the message
      process_message(tick, conn);

      // Remove processed bytes from buffer
      conn.buffer.erase(0, total_message_size);
    }
  }

  return true; // Connection still alive
}

int main() {
  // Create platform-specific event mechanism
#ifdef USE_EPOLL
  int event_fd = epoll_create1(0);
  if (event_fd == -1) {
    LOG_PERROR("Client", "epoll_create1 failed");
    return 1;
  }
  LOG_INFO("Client", "Using epoll (Linux)");
#elif defined(USE_KQUEUE)
  int event_fd = kqueue();
  if (event_fd == -1) {
    LOG_PERROR("Client", "kqueue creation failed");
    return 1;
  }
  LOG_INFO("Client", "Using kqueue (macOS/BSD)");
#endif

  // Connect to 3 exchanges
  std::vector<int> ports = {9999, 10000, 10001};
  std::map<int, Connection> connections; // Map fd -> Connection

  for (int port : ports) {
    int sockfd = connect_to_exchange(port);
    if (sockfd < 0) {
      continue; // Skip failed connections
    }

    // Register with event mechanism
#ifdef USE_EPOLL
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered read events
    ev.data.fd = sockfd;

    if (epoll_ctl(event_fd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
      LOG_PERROR("Client", "epoll_ctl registration failed");
      close(sockfd);
      continue;
    }
#elif defined(USE_KQUEUE)
    struct kevent ev_set;
    EV_SET(&ev_set, sockfd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(event_fd, &ev_set, 1, NULL, 0, NULL) == -1) {
      LOG_PERROR("Client", "kevent registration failed");
      close(sockfd);
      continue;
    }
#endif

    // Store connection
    connections.emplace(sockfd, Connection(sockfd, port));
  }

  if (connections.empty()) {
    LOG_ERROR("Client", "Failed to connect to any exchanges");
    close(event_fd);
    return 1;
  }

  std::cout << "Multiplexing " << connections.size()
            << " exchanges (binary protocol)" << std::endl;

  auto start_time = std::chrono::steady_clock::now();
  int active_connections = connections.size();

  // Store statistics for closed connections
  std::map<int, uint64_t> port_message_counts;

  // Event loop
  while (active_connections > 0) {
#ifdef USE_EPOLL
    struct epoll_event events[10];
    int nev = epoll_wait(event_fd, events, 10, -1);
#elif defined(USE_KQUEUE)
    struct kevent ev_list[10];
    int nev = kevent(event_fd, NULL, 0, ev_list, 10, NULL);
#endif

    if (nev == -1) {
      LOG_PERROR("Client", "event wait failed");
      break;
    }

    // Process each ready socket
    for (int i = 0; i < nev; i++) {
#ifdef USE_EPOLL
      int ready_fd = events[i].data.fd;
#elif defined(USE_KQUEUE)
      int ready_fd = ev_list[i].ident;
#endif

      // Find the connection
      auto it = connections.find(ready_fd);
      if (it == connections.end()) {
        continue; // Shouldn't happen
      }

      Connection &conn = it->second;

      // Read from this connection
      bool still_alive = drain_socket(conn);

      if (!still_alive) {
        // Connection closed or error
        std::cout << "Removing connection to exchange " << conn.port
                  << std::endl;

        // Save statistics before erasing
        port_message_counts[conn.port] = conn.message_count;

#ifdef USE_EPOLL
        epoll_ctl(event_fd, EPOLL_CTL_DEL, conn.sockfd, nullptr);
#endif

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

  // Add statistics from closed connections
  for (const auto &[port, count] : port_message_counts) {
    std::cout << "Exchange " << port << ": " << count << " messages"
              << std::endl;
    total_messages += count;
  }

  // Add statistics from still-active connections (if any)
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
  close(event_fd);

  return 0;
}