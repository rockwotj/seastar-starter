# Seastar starter project

This project contains a small [Seastar](https://github.com/scylladb/seastar)
program with [Wasmtime](https://github.com/bytecodealliance/wasmtime) and minimal cmake scaffolding.

# Getting started

Install dependencies (assuming a recent verison of Ubuntu such as 22.04):

```bash
git submodule update --init --recursive
seastar/install-dependencies.sh
apt-get install -qq ninja-build clang
```

Configure and build:

```
CXX=clang++ CC=clang cmake -Bbuild -S. -GNinja
ninja -C build
```

Run the example program `./main`:

```
build/main
```

Similar output to the following should be produced:

```
INFO  2023-08-10 12:48:36,307 seastar - Reactor backend: linux-aio
INFO  2023-08-10 12:48:36,338 [shard 0] seastar - Created fair group io-queue-0 for 2 queues, capacity rate 2147483:2147483, limit 12582912, rate 16777216 (factor 1), threshold 2000, per tick grab 6291456
INFO  2023-08-10 12:48:36,338 [shard 0] seastar - IO queue uses 0.75ms latency goal for device 0
INFO  2023-08-10 12:48:36,338 [shard 0] seastar - Created io group dev(0), length limit 4194304:4194304, rate 2147483647:2147483647
INFO  2023-08-10 12:48:36,338 [shard 0] seastar - Created io queue dev(0) capacities: 512:2000:2000 1024:3000:3000 2048:5000:5000 4096:9000:9000 8192:17000:17000 16384:33000:33000 32768:65000:65000 65536:129000:129000 131072:257000:257000
Initializing...
Compiling module...
Instantiating module...
Extracting export...
Calling export...
All finished!
Initializing...
Compiling module...
Instantiating module...
Extracting export...
Calling export...
zsh: illegal hardware instruction (core dumped)  ./build/main
```
