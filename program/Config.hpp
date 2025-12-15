#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string_view>

struct Config {
  std::string_view symbol;
  char side;
  std::uint32_t max_order_size;
  std::uint32_t vwap_window_period;
};

#endif
