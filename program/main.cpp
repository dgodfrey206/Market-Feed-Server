#include "Feed.hpp"

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#include <variant>

int main(int argc, char* argv[]) {
  if (argc < 9) {
    std::cout << "Invalid number of arguments\n";
    std::exit(1);
  }

  auto config = Config{
      .symbol = argv[1],
      .side = argv[2][0],
      .max_order_size = static_cast<std::uint32_t>(std::stoul(argv[3])),
      .vwap_window_period = static_cast<std::uint32_t>(std::stoul(argv[4])),
  };

  Feed<Schema::Packet> feed{config, {argv[5], argv[6], argv[7], argv[8]}};

  while (true) {
    feed.forward();
  }
}
