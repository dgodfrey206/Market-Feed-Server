
# Market Feed Server
Market Data Server receives serialized packets over a TCP connection. The packets represent quotes and trades for stocks. As the data is passed through a VWAP calculation is performed, and an order is sent to another socket connection when the order condition is ideal.

## Compiler requirements
The program works for compilers supporting C++14 or greater.

## Details
Information about the name of the stock, the maximum order quantity, and the VWAP window are passed as command line arguments. The feed server recieves quotes and trades and keeps track of the timestamps, prices, and quantities, and uses this to determine when a order can be sent to the client. There must be at least one trade processed before an order can be made, because a trade has information that is incorporated into the VWAP calculation which determines when a new order can be made.

On reciept of a new quote or trade, its timestamp is compared against the earliest timestamp seen in the VWAP window. If the difference (in seconds) is greater than the max window time period given as a command line argument, we check if a trade has been seen earlier. If so, a new order can be made.

The format classes are contained in a namespace `Schema`. Padding is disabled for these classes with a `#pragma pack` statement.

RAII is used for handling sockets.

## Building
From the root project directory run `make`:
```bash
make
```
The resulting executables will appear in the project's `bin` directory. 

Then run the program:
```bash
make run_main
```
 By default it will pass the following command line arguments to `main.cpp`. If you want custom behavior you will have to modify the Makefile or execute `./bin/main` yourself:
 
```bash
IBM B 100 30 127.0.0.1 5000 127.0.0.1 5001
```

## Test

The program can be tested by running the following. Sample packets will be sent to main  from the server and orders will be sent to the client:
```bash
make
make run_server # run on seperate terminal
make run_client # run on seperate terminal
make run_main # run on seperate terminal
```
