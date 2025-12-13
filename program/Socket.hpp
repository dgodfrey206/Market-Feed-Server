#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include <string_view>
#include <string>

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
