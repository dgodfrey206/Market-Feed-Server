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

    template<class... Ts>
    struct overloads : Ts... {
	using Ts::operator()...;
    };

    template<class... Ts>
    overloads(Ts...) -> overloads<Ts...>;
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
	#pragma pack(pop)

        struct Packet {
                #pragma pack(push, 1)
                struct Header {
                        std::uint8_t length;
                        std::uint8_t type;
                } header;
                #pragma pack(pop)
                std::variant<Quote, Trade> body;
        };
}

auto process_order(std::string_view symbol, char side, std::uint64_t timestamp, double vwap, std::optional<Schema::Quote>& quote) {
	std::uint32_t price, *quantity;
	bool betterThan = false;

	Schema::Order order{};
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

template<class Packet>
struct packet_reader {
	bool read_header(int sockfd, Packet& packet) {
		return read(sockfd, &packet.header, sizeof packet.header) == sizeof packet.header;
	}

	template<class Body>
	bool read_body(int sockfd, Body& body) {
		return read(sockfd, &body, sizeof(Body)) == sizeof(Body);
	}
};

template<class Packet>
struct Feed {
	Feed() = delete;
	Feed(Config& config) :
		p_args(config),
		market_sockfd(p_args.market_data_socket.handle())
	{ }

	using Header = typename Packet::Header;
	using Trade = Schema::Trade;
	using Quote = Schema::Quote;
	using Order = Schema::Order;

	void forward() {
		if (!p_args.market_data_socket)
			return;

		if (!reader.read_header(market_sockfd, current_packet))
			return;

                std::cout << "Header length = " << (int)current_packet.header.length << "; Header type = " << (int)current_packet.header.type << '\n';
                if (current_packet.header.length <= 0)
                        return;

		if (current_packet.header.type == 1) {
			current_packet.body = Quote{};
		} else {
			current_packet.body = Trade{};
		}

		auto process_packet_body = [this](auto&& body, auto&& callable) {
			reader.read_body(market_sockfd, body);
			if (strncmp(body.symbol, p_args.symbol.begin(), symbol_length) == 0) {
				callable();
			}
			if (!first_timestamp) first_timestamp = body.timestamp;
			try_process_order(body);
		};

		std::visit(detail::overloads{
			[&](Quote& body) {
				process_packet_body(body, [&] { current_quote = body; });
			},
			[&](Trade& body) {
				process_packet_body(body, [&] {
					pq_sum += body.price * body.quantity;
                                	q_sum += body.quantity;
                                	seen_trade = true;
				});
			}
		}, current_packet.body);

		std::cout << "---------------------------------\n";
	}

	void process_order(std::uint64_t timestamp) {
                ::process_order(p_args.symbol, p_args.side, timestamp, pq_sum / q_sum, current_quote)
                       	([&] (Order order, std::uint32_t price, std::uint32_t quantity) {
                               	order.price = price;
                                order.quantity = std::min(quantity, p_args.max_order_size);
                                send(p_args.order_connection_socket.handle(), &order, sizeof(order), 0);
                        });

                pq_sum = q_sum = 0;
                first_timestamp = timestamp;
	}
private:
	Config& p_args;
	detail::optional<Quote> current_quote;
	Packet current_packet;
	std::optional<std::uint64_t> first_timestamp;
	int market_sockfd;
	int pq_sum = 0;
        int q_sum = 0;
	bool seen_trade = false;
	packet_reader<Packet> reader;
private:

	template<class B>
	void try_process_order(B& body) {
		int diff = (body.timestamp - first_timestamp.value()) / 1'000'000'000ULL;
                std::cout << "body.timestamp: " << body.timestamp << '\n';
                std::cout << "Timestamp diff: " << diff << '\n';
                std::cout << "is " << p_args.vwap_window_period << " second window reached? " << std::boolalpha << (diff >= p_args.vwap_window_period) << '\n';
                if (current_quote && seen_trade && diff >= p_args.vwap_window_period) {
                	std::cout << "PROCESSING ORDER\n";
                        process_order(body.timestamp);
                        std::cout << "New timestamp: " << (body.timestamp) << '\n';
                }
	}
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

	Feed<Schema::Packet> feed{config};


        while (true) {
		feed.forward();

		std::this_thread::sleep_for(200ms);
        }
}
