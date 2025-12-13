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
#include <variant>

namespace detail {
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

        explicit operator bool() const { return success; }

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
        std::uint32_t max_order_size;
        std::uint32_t vwap_window_period;
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
        char symbol[symbol_length];
        std::uint64_t timestamp;
        char side;
        std::uint32_t quantity;
        std::uint32_t price;
};
#pragma pack(pop)

detail::optional<Trade> parse_trade_format(int sockfd, std::uint8_t totalBytes) {
        Trade t{};
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

detail::optional<Quote> parse_quote_format(int sockfd, std::uint8_t totalBytes) {
        Quote q{};
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

auto process_order(std::string_view symbol, char side, std::uint64_t timestamp, double vwap, std::optional<Quote>& quote) {
	std::uint32_t price, *quantity;
	bool betterThan = false;

	Order order{};
        strncpy(order.symbol, symbol.data(), symbol.size());
        order.timestamp = timestamp;
        order.side = side;

	if (side == 'B') {
		price = quote->ask_price;
		quantity = &quote->ask_quantity;
		betterThan = quote->ask_price < vwap;
	} else {
		price = quote->bid_price;
		quantity = &quote->bid_quantity;
		betterThan = quote->bid_price > vwap;
	}
	return [=](auto&& callable) mutable {
		if (*quantity > 0 && betterThan) {
			(decltype(callable)(callable))(order, price, *quantity);
			*quantity -= order.quantity;
		}
	};
}

struct Feed {
	Feed() = delete;
	Feed(Config& config) :
		p_args(config),
		market_sockfd(p_args.market_data_socket.handle())
	{ }

	template<class T>
	struct tag {};

	void parse(tag<Quote>, std::string_view symbol, std::size_t len) {
		parse_quote(market_sockfd, symbol, len).and_then([&](auto&&, auto&& quote) {
                	std::cout << "Successfully parsed quote\n";
                        current_quote = quote;
                });
	}

	void parse(tag<Trade>, std::string_view symbol, std::size_t len) {
		current_trade = parse_trade(market_sockfd, p_args.symbol, len);
                current_trade.and_then([&](auto&& self, Trade const& t) {
                	std::cout << "Updating values\n";
                        pq_sum += t.price * t.quantity;
                        q_sum += t.quantity;
                        if (!first_timestamp) first_timestamp = t.timestamp;
                }).or_else([&] (auto&& self) {
                	perror("Something went wrong parsing the trade. Aborting...");
                        std::exit(1);
                });
	}

	void forward() {
		if (!p_args.market_data_socket)
			return;
		std::byte header[sizeof(std::uint8_t) * 2];
                ssize_t n = read(market_sockfd, header, sizeof header);
                std::cout << "bytes read from header = " << n << '\n';
		std::cout << errno << '\n';
                if (n < 0)
                        return;

		std::uint8_t len = std::to_integer<std::uint8_t>(header[0]);
                std::uint8_t type = std::to_integer<std::uint8_t>(header[1]);
                std::cout << "len = " << (int)len << " type = " << (int)type << '\n';
                if (len <= 0)
                        return;

		if (type == 1) {
			parse(tag<Quote>{}, p_args.symbol, len);
		} else {
			parse(tag<Trade>{}, p_args.symbol, len);
			try_process_order();
		}
	}

	void try_process_order() {
		double diff = (current_trade->timestamp - first_timestamp.value()) / 1'000'000'000ULL;
                std::cout << "Timestamp diff: " << diff << '\n';
                std::cout << "is " << p_args.vwap_window_period << " second window reached? " << std::boolalpha << (diff >= p_args.vwap_window_period) << '\n';

                // if the duration has been reached (trade.timestamp - first_timestamp)
                if (current_quote && current_trade && diff >= p_args.vwap_window_period) {
                	process_order(p_args.symbol, p_args.side, current_trade->timestamp, pq_sum / q_sum, current_quote)
                        	([&] (Order order, std::uint32_t price, std::uint32_t quantity) {
                                	order.price = price;
                                        order.quantity = std::min(quantity, p_args.max_order_size);
                                        send(p_args.order_connection_socket.handle(), &order, sizeof(order), 0);
                                 });

                        pq_sum = q_sum = 0;
                        first_timestamp = current_trade->timestamp;
                }
	}
private:
	Config& p_args;
	detail::optional<Quote> current_quote;
	detail::optional<Trade> current_trade;
	std::optional<std::uint64_t> first_timestamp;
	int market_sockfd;
	int pq_sum = 0;
        int q_sum = 0;
};

int main(int argc, char* argv[]) {
        using namespace std::chrono_literals;

        if (argc < 9) {
                std::cout << "Invalid number of arguments\n";
                std::exit(1);
        }

        auto config = Config{
                .symbol = argv[1],
                .side = argv[2][0],
                .max_order_size = static_cast<std::uint32_t>(std::stoul(argv[3])),
                .vwap_window_period = static_cast<std::uint32_t>(std::stoul(argv[4])),
                .market_data_socket{argv[5], argv[6]},
                .order_connection_socket{argv[7], argv[8]}
        };

	Feed feed{config};


        while (true) {
		feed.forward();

		std::this_thread::sleep_for(200ms);
        }
}
