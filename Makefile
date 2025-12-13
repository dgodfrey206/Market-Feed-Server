CXX = g++
CXXFLAGS = -std=c++17

# Executable names
SERVER = server/server
CLIENT = client/client
MAIN = program/main

# Source files
SERVER_SRC = server/server.cpp
CLIENT_SRC = client/client.cpp
MAIN_SRC = program/main.cpp

.PHONY: all run_server run_client run_main clean

.PHONY: rebuild_main
rebuild_main:
	$(CXX) $(CXXFLAGS) program/main.cpp -o program/main

all: $(SERVER) $(CLIENT) $(MAIN)

$(SERVER): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

$(CLIENT): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

$(MAIN): $(MAIN_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

run_server: $(SERVER)
	./$(SERVER)

run_client: $(CLIENT)
	./$(CLIENT)

run_main: $(MAIN)
	./$(MAIN) IBM B 100 30 127.0.0.1 5000 127.0.0.1 5001

clean:
	rm -f $(SERVER) $(CLIENT) $(MAIN)
