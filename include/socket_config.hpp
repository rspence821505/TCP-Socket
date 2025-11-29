#ifndef SOCKET_CONFIG_HPP
#define SOCKET_CONFIG_HPP

#include <iostream>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

// Socket configuration structure
struct SocketConfig {
  bool tcp_nodelay = false;      // Disable Nagle's algorithm
  int rcvbuf_size = 0;           // Receive buffer size (0 = default)
  int sndbuf_size = 0;           // Send buffer size (0 = default)
  bool tcp_quickack = false;     // Linux-only: immediate ACKs
  
  SocketConfig() = default;
  
  SocketConfig(bool nodelay, int rcvbuf, int sndbuf, bool quickack = false)
    : tcp_nodelay(nodelay), rcvbuf_size(rcvbuf), 
      sndbuf_size(sndbuf), tcp_quickack(quickack) {}
  
  std::string to_string() const {
    std::string result = "SocketConfig{";
    result += "TCP_NODELAY=" + std::string(tcp_nodelay ? "ON" : "OFF");
    if (rcvbuf_size > 0) {
      result += ", SO_RCVBUF=" + std::to_string(rcvbuf_size / 1024) + "KB";
    }
    if (sndbuf_size > 0) {
      result += ", SO_SNDBUF=" + std::to_string(sndbuf_size / 1024) + "KB";
    }
#ifdef __linux__
    if (tcp_quickack) {
      result += ", TCP_QUICKACK=ON";
    }
#endif
    result += "}";
    return result;
  }
  
  std::string short_name() const {
    std::string result;
    if (tcp_nodelay) result += "nodelay_";
    if (rcvbuf_size > 0) {
      result += "rcv" + std::to_string(rcvbuf_size / 1024) + "k_";
    }
    if (sndbuf_size > 0) {
      result += "snd" + std::to_string(sndbuf_size / 1024) + "k_";
    }
    if (tcp_quickack) result += "quickack_";
    if (result.empty()) result = "default_";
    if (result.back() == '_') result.pop_back();
    return result;
  }
};

// Apply socket configuration to a socket
inline bool apply_socket_config(int sockfd, const SocketConfig& config, bool verbose = true) {
  bool success = true;
  
  // 1. TCP_NODELAY: Disable Nagle's algorithm
  if (config.tcp_nodelay) {
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
      perror("setsockopt TCP_NODELAY failed");
      success = false;
    } else if (verbose) {
      std::cout << "  ✓ TCP_NODELAY enabled" << std::endl;
    }
  }
  
  // 2. SO_RCVBUF: Receive buffer size
  if (config.rcvbuf_size > 0) {
    int size = config.rcvbuf_size;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
      perror("setsockopt SO_RCVBUF failed");
      success = false;
    } else if (verbose) {
      // Read back actual size (kernel may adjust)
      socklen_t optlen = sizeof(size);
      getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, &optlen);
      std::cout << "  ✓ SO_RCVBUF set to " << size / 1024 << " KB" << std::endl;
    }
  }
  
  // 3. SO_SNDBUF: Send buffer size
  if (config.sndbuf_size > 0) {
    int size = config.sndbuf_size;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
      perror("setsockopt SO_SNDBUF failed");
      success = false;
    } else if (verbose) {
      // Read back actual size (kernel may adjust)
      socklen_t optlen = sizeof(size);
      getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &size, &optlen);
      std::cout << "  ✓ SO_SNDBUF set to " << size / 1024 << " KB" << std::endl;
    }
  }
  
  // 4. TCP_QUICKACK: Linux-only, immediate ACKs
#ifdef __linux__
  if (config.tcp_quickack) {
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) < 0) {
      perror("setsockopt TCP_QUICKACK failed");
      success = false;
    } else if (verbose) {
      std::cout << "  ✓ TCP_QUICKACK enabled" << std::endl;
    }
  }
#else
  if (config.tcp_quickack && verbose) {
    std::cout << "  ⚠ TCP_QUICKACK not supported on this platform" << std::endl;
  }
#endif
  
  return success;
}

// Query actual socket buffer sizes
struct SocketBufferSizes {
  int rcvbuf;
  int sndbuf;
};

inline SocketBufferSizes get_socket_buffer_sizes(int sockfd) {
  SocketBufferSizes sizes;
  socklen_t optlen = sizeof(int);
  
  getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &sizes.rcvbuf, &optlen);
  getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sizes.sndbuf, &optlen);
  
  return sizes;
}

// Check if TCP_NODELAY is enabled
inline bool is_nodelay_enabled(int sockfd) {
  int flag = 0;
  socklen_t optlen = sizeof(flag);
  getsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, &optlen);
  return flag != 0;
}

#endif // SOCKET_CONFIG_HPP
