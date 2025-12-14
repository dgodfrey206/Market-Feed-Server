#include "Socket.hpp"
#include "Schema.hpp"
#include "Config.hpp"

#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include <string_view>
#include <variant>
#include <optional>
#include <cstdint>
#include <iostream>

#ifndef FEED_HPP
#define FEED_HPP

namespace details {
template <class... Ts>
struct overloads : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
overloads(Ts...) -> overloads<Ts...>;
}  // namespace details


inline constexpr std::size_t symbol_length = 8;

template <class Packet>
class Feed {
  struct socket_addresses {
    std::string_view market_data_ip, market_data_port;
  };

  struct packet_reader {
    bool read_header(int sockfd, Packet& packet) {
      return read(sockfd, &packet.header, sizeof packet.header) == sizeof packet.header;
    }

    template <class Body>
    bool read_body(int sockfd, Body& body) {
      return read(sockfd, &body, sizeof(Body)) == sizeof(Body);
    }
  };

  using Header = typename Packet::Header;
  using Trade = Schema::Trade;
  using Quote = Schema::Quote;

 private:
  Config& p_args;
  socket_addresses addr;
  Socket market_connection;
  std::optional<Quote> current_quote_;
  Packet current_packet;
  std::optional<std::uint64_t> first_timestamp;
  packet_reader reader;
  int market_sockfd;
  int pq_sum = 0;
  int q_sum = 0;
  bool seen_trade = false;
  int timestamp_diff = -1;

 public:
  Feed() = delete;
  Feed(Config& config, socket_addresses addr);

  void forward();

  bool ready() const {
    return current_quote_ && seen_trade && timestamp_diff >= p_args.vwap_window_period;
  }

  void reset_window() {
	pq_sum = q_sum = 0;
	seen_trade = false;
	timestamp_diff = -1;
        std::visit([&](auto const& body) { first_timestamp = body.timestamp; }, current_packet.body);
  }

  std::optional<Quote> get_current_quote() const { return current_quote_; }

  double get_vwap() const { return pq_sum / q_sum; }
};

template<class Packet>
Feed<Packet>::Feed(Config& config, socket_addresses addr)
      : p_args(config),
        addr(addr),
        market_connection(addr.market_data_ip, addr.market_data_port),
        market_sockfd(market_connection.handle()) {}

template<class Packet>
void Feed<Packet>::forward() {
    if (!market_connection) return;

    if (!reader.read_header(market_sockfd, current_packet)) return;

    std::cout << "Header length = " << (int)current_packet.header.length
              << "; Header type = " << (int)current_packet.header.type << '\n';
    if (current_packet.header.length <= 0) return;

    if (current_packet.header.type == 1) {
      current_packet.body = Quote{};
    } else {
      current_packet.body = Trade{};
    }

    auto process_packet_body = [this](auto&& body, auto&& callable) {
      if (!reader.read_body(market_sockfd, body)) {
        perror("Failed to read packet body.");
        return;
      }
      if (strncmp(body.symbol, p_args.symbol.begin(), symbol_length) == 0) {
        std::forward<decltype(callable)>(callable)();
      }
      if (!first_timestamp) first_timestamp = body.timestamp;

      timestamp_diff = (body.timestamp - first_timestamp.value()) / 1'000'000'000ULL;

      std::cout << "Timestamp diff: " << timestamp_diff << '\n';
      //try_process_order(body);
    };

    std::visit(
      details::overloads{[&](Quote& body) {
        process_packet_body(body, [&] { current_quote_ = body; });
      },
      [&](Trade& body) {
        process_packet_body(body, [&] {
          pq_sum += body.price * body.quantity;
          q_sum += body.quantity;
          seen_trade = true;
      });
    }}, current_packet.body);
}

#endif
