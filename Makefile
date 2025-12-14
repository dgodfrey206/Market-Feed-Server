CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

BIN = bin

# Executables
MAIN = $(BIN)/main
SERVER = $(BIN)/server
CLIENT = $(BIN)/client

# Source files
MAIN_SRC = program/main.cpp program/OrderManager.cpp
SERVER_SRC = server/server.cpp
CLIENT_SRC = client/client.cpp

.PHONY: all run_main run_server run_client clean

all: $(BIN) $(MAIN) $(SERVER) $(CLIENT)

# Ensure bin directory exists
$(BIN):
	mkdir -p $(BIN)

# Compile and link main
$(MAIN): $(MAIN_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Compile and link server
$(SERVER): $(SERVER_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@

# Compile and link client
$(CLIENT): $(CLIENT_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@

# Run targets
run_main: $(MAIN)
	./$(MAIN) IBM B 100 30 127.0.0.1 5000 127.0.0.1 5001

run_server: $(SERVER)
	./$(SERVER)

run_client: $(CLIENT)
	./$(CLIENT)

clean:
	rm -rf $(BIN)
