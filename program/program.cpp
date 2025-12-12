#include <iostream>
#include <chrono>
#include <thread>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <optional>
#include <functional>
#include <type_traits>
#include <cerrno>
#include <cassert>

namespace detail {
    template<class T>
    struct remove_cvref {
      using type = std::remove_cv_t<std::remove_reference_t<T>>;
    };

    template<class T>
    using remove_cvref_t = typename remove_cvref<T>::type;

    template<class T>
    struct optional : std::optional<T> {
       using std::optional<T>::optional;
       using std::optional<T>::operator=;

       template<class Callable>
       auto and_then(Callable&& callable) {
        if (*this) {
            std::invoke(std::forward<Callable>(callable), *this, this->value());
        }
	return *this;
       }

       template<class Callable>
	auto value_if(Callable&& callable) {
		if (std::forward<Callable>(callable)(this->value())) {
			return *this;
		}
		return detail::optional<T>{};
	}
       template<class Callable>
       auto or_else(Callable&& callable) {
        if (!*this) {
            std::forward<Callable>(callable)(*this);
        }
	return *this;
       }
    };
}

struct connection {
        explicit connection(std::string_view ip, std::string_view port) :
                sock{socket(AF_INET, SOCK_STREAM, 0)}
        {
                auto p = static_cast<unsigned short>(std::stoul(port.data()));
                sockaddr_in addr{
                        .sin_family = AF_INET,
                        .sin_port = htons(p)
                };

                inet_pton(AF_INET, ip.data(), &addr.sin_addr);
                int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
		success = result == 0;
		assert(success);
        }

        explicit operator bool() const {
                return success;
        }

        int handle() const { return sock; }

        ~connection() {
                close(sock);
        }
private:
        int sock;
        bool success;
};

static constexpr std::size_t symbol_length = 8;

struct Config {
        std::string_view symbol;
        char side;
        unsigned max_order_size;
        unsigned vwap_window_period;
        connection market_data_socket;
        connection order_connection_socket;
};

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
        char symbol[8];
        std::uint64_t timestamp;
        char side;
        std::uint32_t quantity;
        std::uint32_t price;
};
#pragma pack(pop)



detail::optional<Trade> parse_trade_format(int sockfd, std::uint8_t totalBytes) {
        Trade t;
        std::size_t bytes = 0;
        bytes += read(sockfd, t.symbol, symbol_length);
        bytes += read(sockfd, &t.timestamp, sizeof(t.timestamp));
	bytes += read(sockfd, &t.quantity, sizeof(t.quantity));
        bytes += read(sockfd, &t.price, sizeof(t.price));

	std::cout << "Trade Bytes read: " << bytes << '\n';
        if (bytes == totalBytes)
            return t;
        return std::nullopt;
}

Order parse_order_from_trade(Trade const& trade) {
        Order order;
        memcpy(order.symbol, trade.symbol, symbol_length);
        order.timestamp = trade.timestamp;
        order.quantity = trade.quantity;
        order.price = trade.price;
        return order;
}

detail::optional<Quote> parse_quote_format(int sockfd, std::uint8_t totalBytes) {
        Quote q;
        std::size_t bytes = 0;
        bytes += read(sockfd, q.symbol, symbol_length);
	std::cout << "symbol bytes read: " << bytes << ", symbol = " << q.symbol << '\n';
        bytes += read(sockfd, &q.timestamp, sizeof(q.timestamp));
        bytes += read(sockfd, &q.bid_quantity, sizeof(q.bid_quantity));
        bytes += read(sockfd, &q.bid_price, sizeof(q.bid_price));
        bytes += read(sockfd, &q.ask_quantity, sizeof(q.ask_quantity));
        bytes += read(sockfd, &q.ask_price, sizeof(q.ask_price));
        std::cout << "Quote bytes read: " << bytes << '\n';
	if (bytes == totalBytes)
            return q;
        return std::nullopt;
}

auto parse_quote(int sockfd, std::string_view symbol, std::uint8_t totalBytes) {
        // only set if the symbol is equal to config.symbol
        auto quote = parse_quote_format(sockfd, totalBytes);
	std::cout << "New Quote: \n";
	std::cout << "symbol: " << quote->symbol << '\n';
	std::cout << "timestamp: " << quote->timestamp << '\n';
	std::cout << "bid_quantity: " << quote->bid_quantity << '\n';
	std::cout << "bid_price: " << quote->bid_price << '\n';
	std::cout << "ask_quantity: " << quote->ask_quantity << '\n';
	std::cout << "ask_price: " << quote->ask_price << '\n';
        return quote.value_if([&](Quote const& q) {
                return strncmp(q.symbol, symbol.begin(), symbol_length) == 0;
        });
}

auto parse_trade(int sockfd, std::string_view symbol, std::uint8_t totalBytes) {
        auto trade = parse_trade_format(sockfd, totalBytes);
	std::cout << "New Trade: \n";
	std::cout << "symbol: " << trade->symbol << '\n';
	std::cout << "timestamp: " << trade->timestamp << '\n';
	std::cout << "quantity: " << trade->quantity << '\n';
	std::cout << "price: " << trade->price << '\n';

        return trade.value_if([&](Trade const& t) {
                return strncmp(t.symbol, symbol.begin(), symbol_length) == 0;
        });
}

int main(int argc, char* argv[]) {
        using namespace std::chrono_literals;

        if (argc < 9) {
                std::cout << "Invalid number of arguments\n";
                std::exit(0);
        }

        auto config = Config{
                .symbol = argv[1],
                .side = argv[2][0],
                .max_order_size = static_cast<unsigned>(std::stoul(argv[3])),
                .vwap_window_period = static_cast<unsigned>(std::stoul(argv[4])),
                .market_data_socket{argv[5], argv[6]},
                .order_connection_socket{argv[7], argv[8]}
        };

        detail::optional<Quote> last_quote;
        std::optional<std::uint64_t> first_timestamp;
        int market_sockfd = config.market_data_socket.handle();
        int order_sockfd = config.order_connection_socket.handle();
        int d1 = 0;
        int d2 = 0;
        int vwap = 0;

        while (true) {
                if (!config.market_data_socket)
                        break;

                std::byte header[sizeof(std::uint8_t) * 2];
                ssize_t n = read(market_sockfd, header, sizeof header);
		std::cout << "bytes read from header = " << n << '\n';
                if (n < 0)
                        break;

                std::uint8_t len = std::to_integer<std::uint8_t>(header[0]);
                std::uint8_t type = std::to_integer<std::uint8_t>(header[1]);
		std::cout << "len = " << (int)len << " type = " << (int)type << '\n';
                if (len <= 0)
                        break;

                if (type == 1) {
                        parse_quote(market_sockfd, config.symbol, len).and_then([&](auto&& self, auto&& quote) {
			    std::cout << "Successfully parsed quote\n";
                            last_quote = quote;
                        });
                } else {
                        auto trade = parse_trade(market_sockfd, config.symbol, len);
                        trade.and_then([&](auto&& self, Trade const& t) {
				std::cout << "Updating values\n";
                                d1 += t.price * t.quantity;
                                d2 += t.quantity;
				if (!first_timestamp) first_timestamp = t.timestamp;
                        }).or_else([&] (auto&& self) {
                                perror("Something went wrong parsing the trade. Aborting...");
                                std::exit(1);
                        });

			std::uint64_t diff = (trade->timestamp - first_timestamp.value()) / 1'000'000'000ULL;
			std::cout << "Timestamp diff: " << diff << '\n';
			std::cout << "is " << config.vwap_window_period << " second window reached? " << std::boolalpha << (diff >= config.vwap_window_period) << '\n';

                        // if the duration has been reached (trade.timestamp - first_timestamp)
                        if (last_quote && trade && (trade->timestamp - first_timestamp.value()) / 1e9 >= config.vwap_window_period) {
                                vwap = d1 / d2;
                                //Order order = parse_order_from_trade(trade.value());
                                Order order{};
				strncpy(order.symbol, config.symbol.data(), config.symbol.size());
				order.timestamp = trade->timestamp;
				order.side = config.side;

                                if (config.side == 'B' && last_quote->ask_quantity > 0 && last_quote->ask_price < vwap) {
                                //      send min(last_quote.askQuantity, max_quantity) orders
					order.price = last_quote->ask_price;
                                        order.quantity = std::min(last_quote->ask_quantity, config.max_order_size);
					last_quote->ask_quantity -= order.quantity;
					std::cout << "Sending quantity: " << order.quantity << '\n';
					send(order_sockfd, &order, sizeof order, 0);
                                }
                                if (config.side == 'S' && last_quote->bid_quantity > 0 && last_quote->bid_price > vwap) {
                                //      send min(last_quote.bidQuantity, config.quant)
					order.price = last_quote->bid_price;
                                        order.quantity = std::min(last_quote->bid_quantity, config.max_order_size);
					last_quote->bid_quantity -= order.quantity;
					send(order_sockfd, &order, sizeof order, 0);
                                }

                                //send(order_sockfd, &order, sizeof order, 0);

                                d1 = d2 = 0;
                                first_timestamp = trade->timestamp;
                        }
                }


                std::this_thread::sleep_for(1000ms);
        }
}
