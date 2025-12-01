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

#include "common.hpp"

class ConnectionManagerV2 {
public:
  // State machine for snapshot recovery
  enum class State {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    SNAPSHOT_REQUEST,    // Requesting snapshot
    SNAPSHOT_REPLAY,     // Processing snapshot data
    INCREMENTAL,         // Processing incremental updates
    RECONNECTING
  };
  
  ConnectionManagerV2(const std::string& host, int port, 
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
    , snapshot_requested_(false)
  {}
  
  ~ConnectionManagerV2() {
    disconnect();
  }
  
  // Initial connection
  bool connect() {
    if (state_ == State::CONNECTED || state_ == State::INCREMENTAL) {
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
      snapshot_requested_ = false;  // Will need to request snapshot
      
      std::cout << "[ConnectionManager] ✅ Connected successfully" << std::endl;
      std::cout << "[ConnectionManager] State: CONNECTED → SNAPSHOT_REQUEST" << std::endl;
      
      return true;
    } else {
      state_ = State::DISCONNECTED;
      std::cerr << "[ConnectionManager] ❌ Connection failed" << std::endl;
      return false;
    }
  }
  
  // Reconnect with exponential backoff
  bool reconnect() {
    disconnect();
    
    reconnect_attempts_++;
    state_ = State::RECONNECTING;
    
    std::cout << "[ConnectionManager] 🔄 Reconnecting... (attempt " << reconnect_attempts_ 
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
  
  // State transitions
  void transition_to_snapshot_request() {
    if (state_ == State::CONNECTED) {
      state_ = State::SNAPSHOT_REQUEST;
      snapshot_requested_ = false;
      std::cout << "[ConnectionManager] State: CONNECTED → SNAPSHOT_REQUEST" << std::endl;
    }
  }
  
  void mark_snapshot_requested() {
    snapshot_requested_ = true;
  }
  
  void transition_to_snapshot_replay() {
    if (state_ == State::SNAPSHOT_REQUEST) {
      state_ = State::SNAPSHOT_REPLAY;
      std::cout << "[ConnectionManager] State: SNAPSHOT_REQUEST → SNAPSHOT_REPLAY" << std::endl;
    }
  }
  
  void transition_to_incremental() {
    if (state_ == State::SNAPSHOT_REPLAY) {
      state_ = State::INCREMENTAL;
      std::cout << "[ConnectionManager] ✅ State: SNAPSHOT_REPLAY → INCREMENTAL" << std::endl;
      std::cout << "[ConnectionManager] 📊 Now processing live incremental updates" << std::endl;
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
  
  // Check if we need to request snapshot
  bool needs_snapshot_request() const {
    return state_ == State::SNAPSHOT_REQUEST && !snapshot_requested_;
  }
  
  // Getters
  int sockfd() const { return sockfd_; }
  State state() const { return state_; }
  bool is_connected() const { 
    return state_ == State::CONNECTED || 
           state_ == State::SNAPSHOT_REQUEST ||
           state_ == State::SNAPSHOT_REPLAY ||
           state_ == State::INCREMENTAL; 
  }
  bool is_incremental_mode() const { return state_ == State::INCREMENTAL; }
  int reconnect_attempts() const { return reconnect_attempts_; }
  
  // State name for logging
  const char* state_name() const {
    switch (state_) {
      case State::DISCONNECTED: return "DISCONNECTED";
      case State::CONNECTING: return "CONNECTING";
      case State::CONNECTED: return "CONNECTED";
      case State::SNAPSHOT_REQUEST: return "SNAPSHOT_REQUEST";
      case State::SNAPSHOT_REPLAY: return "SNAPSHOT_REPLAY";
      case State::INCREMENTAL: return "INCREMENTAL";
      case State::RECONNECTING: return "RECONNECTING";
      default: return "UNKNOWN";
    }
  }
  
private:
  int create_and_connect() {
    SocketOptions opts;
    opts.non_blocking = true;

    auto result = socket_connect(host_, port_, opts);
    if (!result) {
      std::cerr << "connection failed: " << result.error() << std::endl;
      return -1;
    }

    return result.value();
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
  bool snapshot_requested_;
};

// Backward compatibility alias
using ConnectionManager = ConnectionManagerV2;

#endif // CONNECTION_MANAGER_HPP
