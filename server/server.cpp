#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <iostream>
#include <chrono>
using namespace std::chrono_literals;

// Server program for testing

#pragma pack(push, 1)
struct Header {
    uint8_t length;
    uint8_t type;
};

struct Quote {
    char symbol[8];
    uint64_t timestamp;
    uint32_t bid_qty;
    uint32_t bid_price;
    uint32_t ask_qty;
    uint32_t ask_price;
};

struct Trade {
    char symbol[8];
    uint64_t timestamp;
    uint32_t quantity;
    int32_t price;
};
#pragma pack(pop)

int server_fd, client_fd;
std::uint64_t timestamp = 1700000000000000000ULL;

void send_quote_impl() {
	Quote q{};
        memcpy(q.symbol, "IBM\0\0\0\0\0", 8);
        std::cout << q.symbol << '\n';
        q.timestamp = timestamp;
        q.bid_qty = 10;
        q.bid_price = 18500;
        q.ask_qty = 12;
        q.ask_price = 18510;

        Header h;
        h.length = sizeof(Quote);
        h.type = 1;

        send(client_fd, &h, sizeof(h), 0);
        send(client_fd, &q, sizeof(q), 0);

        std::cout << "Sent a Quote (" << sizeof(Quote) << " bytes)\n";
	timestamp += 5'000'000'000ULL;
}

void send_trade_impl() {
	Trade t{};
        memcpy(t.symbol, "IBM\0\0\0\0\0", 8);
        t.timestamp = timestamp;
        t.quantity = 25;
        t.price = 32000;

        Header h;
        h.length = sizeof(Trade);
        h.type = 2;

        send(client_fd, &h, sizeof(h), 0);
        send(client_fd, &t, sizeof(t), 0);

        std::cout << "Sent a Trade (" << sizeof(Trade) << " bytes)\n";
	timestamp += 5'000'000'000ULL;
}

int PORT = 5000;

int main(int argc, char* argv[]) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (argc > 1) PORT = std::stoi(argv[1]);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    std::cout << "Server listening on port " << PORT << "...\n";
    client_fd = accept(server_fd, nullptr, nullptr);

    send_quote_impl();

    // --- choose which to send ---
    bool send_quote = false;
    while (1) {
      if (send_quote) {
	send_quote_impl();
      } else {
	send_trade_impl();
      }
      //timestamp += 5'000'000'000ULL;
      std::this_thread::sleep_for(500ms);
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
