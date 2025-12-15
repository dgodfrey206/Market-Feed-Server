#ifndef SCHEMA_HPP
#define SCHEMA_HPP

#include <cstdint>
#include <string_view>
#include <variant>

namespace Schema {
#pragma pack(push, 1)
struct Quote {
  char symbol[8];
  std::uint64_t timestamp;
  std::uint32_t bid_quantity, bid_price;
  std::uint32_t ask_quantity, ask_price;
};

struct Trade {
  char symbol[8];
  std::uint64_t timestamp;
  std::uint32_t quantity;
  std::uint32_t price;
};

struct Order {
  char symbol[8];
  std::uint64_t timestamp;
  char side;
  std::uint32_t quantity;
  std::uint32_t price;
};

struct Packet {
  struct Header {
    std::uint8_t length;
    std::uint8_t type;
  } header;
  std::variant<Quote, Trade> body;
};
#pragma pack(pop)
}  // namespace Schema

#endif
