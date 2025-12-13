CXX = g++
CXXFLAGS = -std=c++17

BIN = bin
SERVER = $(BIN)/server
CLIENT = $(BIN)/client
MAIN = $(BIN)/main

SERVER_SRC = server/server.cpp
CLIENT_SRC = client/client.cpp
MAIN_SRC = program/main.cpp

.PHONY: all run_server run_client run_main clean

all: $(BIN) $(SERVER) $(CLIENT) $(MAIN)

# Make sure bin directory exists
$(BIN):
	mkdir -p $(BIN)

$(SERVER): $(SERVER_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@

$(CLIENT): $(CLIENT_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@

$(MAIN): $(MAIN_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@

run_server: $(SERVER)
	./$(SERVER)

run_client: $(CLIENT)
	./$(CLIENT)

run_main: $(MAIN)
	./$(MAIN) IBM B 100 30 127.0.0.1 5000 127.0.0.1 5001

clean:
	rm -rf $(BIN)
