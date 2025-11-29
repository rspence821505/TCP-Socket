#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class ConnectionManager {
public:
  enum class State {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING
  };
  
  ConnectionManager(const std::string& host, int port, 
                   std::chrono::seconds heartbeat_timeout = std::chrono::seconds(2),
                   std::chrono::seconds max_backoff = std::chrono::seconds(30))
    : host_(host), port_(port)
    , heartbeat_timeout_(heartbeat_timeout)
    , max_backoff_(max_backoff)
    , state_(State::DISCONNECTED)
    , sockfd_(-1)
    , reconnect_attempts_(0)
    , current_backoff_(std::chrono::seconds(1))
    , last_message_time_(std::chrono::steady_clock::now())
  {}
  
  ~ConnectionManager() {
    disconnect();
  }
  
  // Initial connection
  bool connect() {
    if (state_ == State::CONNECTED) {
      return true;
    }
    
    state_ = State::CONNECTING;
    std::cout << "[ConnectionManager] Connecting to " << host_ << ":" << port_ << std::endl;
    
    sockfd_ = create_and_connect();
    
    if (sockfd_ >= 0) {
      state_ = State::CONNECTED;
      reconnect_attempts_ = 0;
      current_backoff_ = std::chrono::seconds(1);
      last_message_time_ = std::chrono::steady_clock::now();
      std::cout << "[ConnectionManager] Connected successfully" << std::endl;
      return true;
    } else {
      state_ = State::DISCONNECTED;
      std::cerr << "[ConnectionManager] Connection failed" << std::endl;
      return false;
    }
  }
  
  // Reconnect with exponential backoff
  bool reconnect() {
    disconnect();
    
    reconnect_attempts_++;
    state_ = State::RECONNECTING;
    
    std::cout << "[ConnectionManager] Reconnecting... (attempt " << reconnect_attempts_ 
              << ", backoff " << current_backoff_.count() << "s)" << std::endl;
    
    // Sleep for backoff period
    std::this_thread::sleep_for(current_backoff_);
    
    // Double backoff for next time, up to max
    current_backoff_ = std::min(current_backoff_ * 2, max_backoff_);
    
    return connect();
  }
  
  // Disconnect
  void disconnect() {
    if (sockfd_ >= 0) {
      close(sockfd_);
      sockfd_ = -1;
    }
    if (state_ != State::RECONNECTING) {
      state_ = State::DISCONNECTED;
    }
  }
  
  // Update last message time (call this when any message is received)
  void update_last_message_time() {
    last_message_time_ = std::chrono::steady_clock::now();
  }
  
  // Check if heartbeat timeout has occurred
  bool is_heartbeat_timeout() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_message_time_);
    return elapsed >= heartbeat_timeout_;
  }
  
  // Get time since last message
  double seconds_since_last_message() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_message_time_);
    return elapsed.count() / 1000.0;
  }
  
  // Getters
  int sockfd() const { return sockfd_; }
  State state() const { return state_; }
  bool is_connected() const { return state_ == State::CONNECTED; }
  int reconnect_attempts() const { return reconnect_attempts_; }
  
private:
  int create_and_connect() {
    // Create socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
      perror("socket creation failed");
      return -1;
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
    
    // Connect to server
    if (::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      perror("connection failed");
      close(sockfd);
      return -1;
    }
    
    // Set non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
      perror("fcntl failed");
      close(sockfd);
      return -1;
    }
    
    return sockfd;
  }
  
  std::string host_;
  int port_;
  std::chrono::seconds heartbeat_timeout_;
  std::chrono::seconds max_backoff_;
  
  State state_;
  int sockfd_;
  int reconnect_attempts_;
  std::chrono::seconds current_backoff_;
  std::chrono::steady_clock::time_point last_message_time_;
};

#endif // CONNECTION_MANAGER_HPP
