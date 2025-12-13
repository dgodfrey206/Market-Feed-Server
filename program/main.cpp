#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <optional>
#include <thread>
#include <type_traits>
#include <variant>

namespace details {
template <class... Ts>
struct overloads : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
overloads(Ts...) -> overloads<Ts...>;
}  // namespace details

struct Socket {
  explicit Socket(std::string_view ip, std::string_view port)
      : sock{socket(AF_INET, SOCK_STREAM, 0)} {
    auto p = static_cast<unsigned short>(std::stoul(port.data()));
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(p)};

    inet_pton(AF_INET, ip.data(), &addr.sin_addr);
    int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
    success = result == 0;
    assert(success);
  }

  explicit operator bool() const { return success; }

  int handle() const { return sock; }

  ~Socket() { close(sock); }

 private:
  int sock;
  bool success;
};

static constexpr std::size_t symbol_length = 8;

struct Config {
  std::string_view symbol;
  char side;
  std::uint32_t max_order_size;
  std::uint32_t vwap_window_period;
};

namespace Schema {
#pragma pack(push, 1)
struct Quote {
  char symbol[symbol_length];
  std::uint64_t timestamp;
  std::uint32_t bid_quantity, bid_price;
  std::uint32_t ask_quantity, ask_price;
};

struct Trade {
  char symbol[symbol_length];
  std::uint64_t timestamp;
  std::uint32_t quantity;
  std::uint32_t price;
};

struct Order {
  char symbol[symbol_length];
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

template <class Packet>
class Feed {
  struct socket_addresses {
    std::string_view market_data_ip, market_data_port;
    std::string_view order_data_ip, order_data_port;
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
  using Order = Schema::Order;

 private:
  Config& p_args;
  socket_addresses addr;
  Socket market_connection;
  std::optional<Quote> current_quote;
  Packet current_packet;
  std::optional<std::uint64_t> first_timestamp;
  packet_reader reader;
  int market_sockfd;
  int pq_sum = 0;
  int q_sum = 0;
  bool seen_trade = false;

 public:
  Feed() = delete;
  Feed(Config& config, socket_addresses addr)
      : p_args(config),
        addr(addr),
        market_connection(addr.market_data_ip, addr.market_data_port),
        market_sockfd(market_connection.handle()) {}

  auto process_order_impl(std::uint64_t timestamp, double vwap);

  void forward() {
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
      try_process_order(body);
    };

    std::visit(
      details::overloads{[&](Quote& body) {
        process_packet_body(body, [&] { current_quote = body; });
      },
      [&](Trade& body) {
        process_packet_body(body, [&] {
          pq_sum += body.price * body.quantity;
          q_sum += body.quantity;
        seen_trade = true;
      });
    }}, current_packet.body);

    std::cout << "---------------------------------\n";
  }

  void process_order(std::uint64_t timestamp) {
    process_order_impl(timestamp, pq_sum / q_sum)(
        [&](Order order, std::uint32_t price, std::uint32_t quantity) {
          Socket order_connection(addr.order_data_ip, addr.order_data_port);
          order.price = price;
          order.quantity = std::min(quantity, p_args.max_order_size);
          send(order_connection.handle(), &order, sizeof(order), 0);
        });

    pq_sum = q_sum = 0;
    first_timestamp = timestamp;
  }

 private:
  template <class B>
  void try_process_order(B& body) {
    int diff = (body.timestamp - first_timestamp.value()) / 1'000'000'000ULL;
    std::cout << "body.timestamp: " << body.timestamp << '\n';
    std::cout << "Timestamp diff: " << diff << '\n';
    std::cout << "is " << p_args.vwap_window_period
              << " second window reached? " << std::boolalpha
              << (diff >= p_args.vwap_window_period) << '\n';
    if (current_quote && seen_trade && diff >= p_args.vwap_window_period) {
      std::cout << "PROCESSING ORDER\n";
      process_order(body.timestamp);
      std::cout << "New timestamp: " << (body.timestamp) << '\n';
    }
  }
};

template <class Packet>
auto Feed<Packet>::process_order_impl(std::uint64_t timestamp, double vwap) {
  std::uint32_t price, quantity;
  bool betterThan = false;

  Schema::Order order{};
  strncpy(order.symbol, p_args.symbol.data(), symbol_length);
  order.timestamp = timestamp;
  order.side = p_args.side;

  if (p_args.side == 'B') {
    std::tie(price, quantity, betterThan) = std::forward_as_tuple(
        current_quote->ask_price, current_quote->ask_quantity,
        current_quote->ask_price < vwap);
  } else {
    std::tie(price, quantity, betterThan) = std::forward_as_tuple(
        current_quote->bid_price, current_quote->bid_quantity,
        current_quote->bid_price > vwap);
  }
  return [=](auto&& callable) mutable {
    if (quantity > 0 && betterThan) {
      (decltype(callable)(callable))(order, price, quantity);
    }
  };
}

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
