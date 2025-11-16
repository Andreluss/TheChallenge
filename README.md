# TheChallenge

I spent 5h today and implemented the Core requirements 1 and 2 (C++). 

## Build 
I recommend importing the project to vscode with C++ and CMake Tools extensions.  
Alternatively, in the project folder: 
```
mkdir build
/usr/bin/cmake --build TheChallenge/build --config Debug --target all -j 8 --
```

## How to run 
### TCP Streaming between 2 processes
```
# terminal 1
./mbo_app --mode=streamer --dbn=../data/CLX5_mbo.dbn --port=9000 --rate=200000

# terminal 2
./mbo_app --mode=engine --host=127.0.0.1 --port=9000 --out=..data/stream_book.json
```
### Replay
Just loads the order data and processes it within the same binary, skipping the network stack.
```
./mbo_app --mode=replay --dbn=../data/CLX5_mbo.dbn --out=../data/book.json
```

## Architecture 
One binary, can be run with 3 modes:
- streamer (loads and streams market data with chosen rate)
- engine (connects to streamer and processes data into order book, discarding wrong messages)
- replay (streamer + engine without the network stack)

Both engine and replay output the latency numbers in p99. 
The code relies on the Databento's structs (MboMsg) and the example attached to the problem description: https://databento.com/docs/examples/order-book/limit-order-book/example 
I used ChatGPT to generate obvious parts of the code. 