#ifndef SOCKET_CONFIG_HPP
#define SOCKET_CONFIG_HPP

#include <iostream>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.hpp"

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
      result += ", SO_RCVBUF=" + format_bytes(rcvbuf_size);
    }
    if (sndbuf_size > 0) {
      result += ", SO_SNDBUF=" + format_bytes(sndbuf_size);
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
inline Result<void> apply_socket_config(int sockfd, const SocketConfig& config, bool verbose = true) {
  // 1. TCP_NODELAY: Disable Nagle's algorithm
  if (config.tcp_nodelay) {
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
      return Result<void>::error("setsockopt TCP_NODELAY failed: " + std::string(strerror(errno)));
    } else if (verbose) {
      std::cout << "  TCP_NODELAY enabled" << std::endl;
    }
  }

  // 2. SO_RCVBUF: Receive buffer size
  if (config.rcvbuf_size > 0) {
    int size = config.rcvbuf_size;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
      return Result<void>::error("setsockopt SO_RCVBUF failed: " + std::string(strerror(errno)));
    } else if (verbose) {
      // Read back actual size (kernel may adjust)
      socklen_t optlen = sizeof(size);
      getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &size, &optlen);
      std::cout << "  SO_RCVBUF set to " << format_bytes(size) << std::endl;
    }
  }

  // 3. SO_SNDBUF: Send buffer size
  if (config.sndbuf_size > 0) {
    int size = config.sndbuf_size;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
      return Result<void>::error("setsockopt SO_SNDBUF failed: " + std::string(strerror(errno)));
    } else if (verbose) {
      // Read back actual size (kernel may adjust)
      socklen_t optlen = sizeof(size);
      getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &size, &optlen);
      std::cout << "  SO_SNDBUF set to " << format_bytes(size) << std::endl;
    }
  }

  // 4. TCP_QUICKACK: Linux-only, immediate ACKs
#ifdef __linux__
  if (config.tcp_quickack) {
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) < 0) {
      return Result<void>::error("setsockopt TCP_QUICKACK failed: " + std::string(strerror(errno)));
    } else if (verbose) {
      std::cout << "  TCP_QUICKACK enabled" << std::endl;
    }
  }
#else
  if (config.tcp_quickack && verbose) {
    std::cout << "  TCP_QUICKACK not supported on this platform" << std::endl;
  }
#endif

  return Result<void>();
}

// Query actual socket buffer sizes
struct SocketBufferSizes {
  int rcvbuf;
  int sndbuf;
};

inline Result<SocketBufferSizes> get_socket_buffer_sizes(int sockfd) {
  SocketBufferSizes sizes;
  socklen_t optlen = sizeof(int);

  if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &sizes.rcvbuf, &optlen) < 0) {
    return Result<SocketBufferSizes>::error("getsockopt SO_RCVBUF failed: " + std::string(strerror(errno)));
  }
  if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sizes.sndbuf, &optlen) < 0) {
    return Result<SocketBufferSizes>::error("getsockopt SO_SNDBUF failed: " + std::string(strerror(errno)));
  }

  return Result<SocketBufferSizes>(sizes);
}

// Check if TCP_NODELAY is enabled
inline Result<bool> is_nodelay_enabled(int sockfd) {
  int flag = 0;
  socklen_t optlen = sizeof(flag);
  if (getsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, &optlen) < 0) {
    return Result<bool>::error("getsockopt TCP_NODELAY failed: " + std::string(strerror(errno)));
  }
  return Result<bool>(flag != 0);
}

#endif // SOCKET_CONFIG_HPP
