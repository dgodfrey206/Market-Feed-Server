#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#pragma pack(push, 1)
struct Order {
    char symbol[8];
    uint64_t timestamp;
    char side;       // 'B' = Buy, 'S' = Sell
    uint32_t quantity;
    int32_t price;
};
#pragma pack(pop)

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5001);  // listening port
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    std::cout << "Server listening on port 5001...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);

    Order order{};
    ssize_t bytes_received = recv(client_fd, &order, sizeof(order), MSG_WAITALL);
    if (bytes_received != sizeof(order)) {
        std::cerr << "Failed to receive full Order struct\n";
        close(client_fd);
        close(server_fd);
        return 1;
    }

    // Print the order
    std::cout << "Received Order:\n";
    std::cout << "Symbol: " << std::string(order.symbol, 8) << "\n";
    std::cout << "Timestamp: " << order.timestamp << "\n";
    std::cout << "Side: " << order.side << "\n";
    std::cout << "Quantity: " << order.quantity << "\n";
    std::cout << "Price: " << order.price << " pennies\n";

    close(client_fd);
    close(server_fd);
}
