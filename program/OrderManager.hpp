#include "Config.hpp"
#include "Schema.hpp"

#include <string_view>
#include <optional>

#ifndef ORDER_MANAGER_HPP
#define ORDER_MANAGER_HPP

struct OrderManager {
    OrderManager(Config const& config, std::string_view address, std::string_view port);

    void process_order(std::optional<Schema::Quote> const&, double);
private:
    std::string_view address, port;
    Config const& p_args;
};

#endif
