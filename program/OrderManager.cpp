#include "OrderManager.hpp"
#include "Socket.hpp"

#include <tuple>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

OrderManager::OrderManager(Config const& config, std::string_view address, std::string_view port)
        : p_args(config), address(address), port(port) {}

void OrderManager::process_order(std::optional<Schema::Quote> const& quote, double vwap) {
  std::uint32_t price, quantity;
  bool betterThan = false;

  Schema::Order order{};
  strncpy(order.symbol, p_args.symbol.data(), p_args.symbol.size());
  order.timestamp = quote->timestamp;
  order.side = p_args.side;

  if (p_args.side == 'B') {
    std::tie(price, quantity, betterThan) = std::forward_as_tuple(
        quote->ask_price, quote->ask_quantity,
        quote->ask_price < vwap);
  } else {
    std::tie(price, quantity, betterThan) = std::forward_as_tuple(
        quote->bid_price, quote->bid_quantity,
        quote->bid_price > vwap);
  }

  if (quantity > 0 && betterThan) {
    std::cout << "Connecting to " << address << " " << port << '\n';
    Socket order_connection(address, port);
    order.price = price;
    order.quantity = std::min(quantity, p_args.max_order_size);
    send(order_connection.handle(), &order, sizeof(order), 0);
  }
}
