#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "binary_protocol.hpp"

class OrderBook {
public:
  OrderBook() = default;
  
  // Clear the book (for snapshot replacement)
  void clear() {
    bids_.clear();
    asks_.clear();
  }
  
  // Load snapshot data
  void load_snapshot(const std::vector<OrderBookLevel>& bids,
                    const std::vector<OrderBookLevel>& asks) {
    clear();
    
    for (const auto& level : bids) {
      if (level.quantity > 0) {
        bids_[level.price] = level.quantity;
      }
    }
    
    for (const auto& level : asks) {
      if (level.quantity > 0) {
        asks_[level.price] = level.quantity;
      }
    }
  }
  
  // Apply incremental update
  void apply_update(uint8_t side, float price, int64_t quantity) {
    auto& book_side = (side == 0) ? bids_ : asks_;
    
    if (quantity == 0) {
      // Delete level
      book_side.erase(price);
    } else if (quantity > 0) {
      // Add or update level
      book_side[price] = static_cast<uint64_t>(quantity);
    } else {
      // Negative quantity is invalid
      std::cerr << "[OrderBook] Invalid quantity: " << quantity << std::endl;
    }
  }
  
  // Get best bid/ask
  bool get_best_bid(float& price_out, uint64_t& quantity_out) const {
    if (bids_.empty()) return false;
    
    auto it = bids_.rbegin();  // Highest bid
    price_out = it->first;
    quantity_out = it->second;
    return true;
  }
  
  bool get_best_ask(float& price_out, uint64_t& quantity_out) const {
    if (asks_.empty()) return false;
    
    auto it = asks_.begin();  // Lowest ask
    price_out = it->first;
    quantity_out = it->second;
    return true;
  }
  
  // Get top N levels
  std::vector<OrderBookLevel> get_top_bids(size_t n) const {
    std::vector<OrderBookLevel> result;
    result.reserve(std::min(n, bids_.size()));
    
    auto it = bids_.rbegin();
    for (size_t i = 0; i < n && it != bids_.rend(); ++i, ++it) {
      result.push_back({it->first, it->second});
    }
    
    return result;
  }
  
  std::vector<OrderBookLevel> get_top_asks(size_t n) const {
    std::vector<OrderBookLevel> result;
    result.reserve(std::min(n, asks_.size()));
    
    auto it = asks_.begin();
    for (size_t i = 0; i < n && it != asks_.end(); ++i, ++it) {
      result.push_back({it->first, it->second});
    }
    
    return result;
  }
  
  // Get depth
  size_t bid_depth() const { return bids_.size(); }
  size_t ask_depth() const { return asks_.size(); }
  bool empty() const { return bids_.empty() && asks_.empty(); }
  
  // Print top of book
  void print_top_of_book(const std::string& symbol) const {
    float bid_price, ask_price;
    uint64_t bid_qty, ask_qty;
    
    bool has_bid = get_best_bid(bid_price, bid_qty);
    bool has_ask = get_best_ask(ask_price, ask_qty);
    
    std::cout << "[" << symbol << "] ";
    
    if (has_bid) {
      std::cout << "BID: $" << bid_price << " @ " << bid_qty << " | ";
    } else {
      std::cout << "BID: --- | ";
    }
    
    if (has_ask) {
      std::cout << "ASK: $" << ask_price << " @ " << ask_qty;
    } else {
      std::cout << "ASK: ---";
    }
    
    if (has_bid && has_ask) {
      float spread = ask_price - bid_price;
      float mid = (bid_price + ask_price) / 2.0f;
      std::cout << " | MID: $" << mid << " | SPREAD: $" << spread;
    }
    
    std::cout << std::endl;
  }
  
  // Print full depth (for debugging)
  void print_depth(const std::string& symbol, size_t levels = 5) const {
    std::cout << "\n=== Order Book: " << symbol << " ===" << std::endl;
    
    auto top_asks = get_top_asks(levels);
    auto top_bids = get_top_bids(levels);
    
    // Print asks (inverted order for visual clarity)
    std::cout << "ASKS:" << std::endl;
    for (auto it = top_asks.rbegin(); it != top_asks.rend(); ++it) {
      std::cout << "  $" << it->price << " @ " << it->quantity << std::endl;
    }
    
    std::cout << "  ----------" << std::endl;
    
    // Print bids
    std::cout << "BIDS:" << std::endl;
    for (const auto& level : top_bids) {
      std::cout << "  $" << level.price << " @ " << level.quantity << std::endl;
    }
    
    std::cout << "=======================" << std::endl;
  }
  
private:
  // Price -> Quantity
  // Bids: higher price is better (use reverse iterator)
  // Asks: lower price is better (use forward iterator)
  std::map<float, uint64_t> bids_;
  std::map<float, uint64_t> asks_;
};

#endif // ORDER_BOOK_HPP
