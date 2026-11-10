# AeronProject

C++23 demo that fans several [Aeron](https://github.com/MonsterPerformance/AeronFramework) channels into one consumer through a [lock-free queue](https://github.com/MonsterPerformance/LockFreeQueue). A publisher floods one channel with random messages; a subscriber listens on four UDP channels and one IPC channel at once, hands every message to a single `mpsc::multiRingQueue` and prints it from the main thread.

| Target            | Source                    | Links against                                    | Purpose                                                       |
| ----------------- | ------------------------- | ------------------------------------------------ | ------------------------------------------------------------- |
| `aeron_subscriber` | `src/aeron_subscriber.cpp` | `LockFreeQueue::LockFreeQueue`, `Aeron::AeronGateway` | one thread per channel, all feeding one `mpsc::multiRingQueue` |
| `aeron_publisher`  | `src/aeron_publisher.cpp`  | `Aeron::AeronGateway`                            | sends 100,000,000 random messages to one channel              |

`src/aeron_common.hpp` holds `pinThreadToCore()`, shared by both programs.

## Dependencies

| Dependency                                                                | CMake package                                         | Used for                                               |
| ------------------------------------------------------------------------- | ----------------------------------------------------- | ------------------------------------------------------ |
| [LockFreeQueue](https://github.com/MonsterPerformance/LockFreeQueue)       | `find_package(LockFreeQueue REQUIRED)` → `LockFreeQueue::LockFreeQueue` | header-only queues; this project uses `<mpsc/multiRingQueue.hpp>` |
| [AeronFramework](https://github.com/MonsterPerformance/AeronFramework)     | `find_package(Aeron REQUIRED)` → `Aeron::AeronGateway`  | static Aeron client and C++ wrapper, plus the `aeron_driver` media driver |

Both have to be installed before this project is configured (see [Build](#build)).

## Rules

- Start order matters: media driver first, then the subscriber, then the publishers. A publisher waits in `isConnected()` until a subscriber listens on its channel and stream.
- Channels and the stream ID (`73`) are compile-time constants in both source files. The subscriber listens on `aeron:udp?endpoint=127.0.0.1:40001` to `:40004` and on `aeron:ipc?endpoint=IPC`. A publisher on any other port never connects.
- Every channel gets its own ring of 65,536 `std::string` slots (`queueSize`). The result of `push` is ignored, so when a ring is full the message is dropped silently. Order is FIFO per channel and undefined across channels.
- The subscriber pins its main thread to core `9` and its five channel threads to cores `4, 5, 11, 12, 13`. It needs at least 14 logical CPUs; on a smaller machine `pthread_setaffinity_np` throws and the process terminates. Change `mainLoopCoreID` and `brokerCoreID` in `src/aeron_subscriber.cpp`, or remove/comment `#define THREAD_PINNING_ENABLED` from `src/aeron_common.hpp` to turn pinning off.
- Pinning alone does not isolate a core: the scheduler can still run other tasks there. For true core isolation the GRUB configuration has to be updated, see [Core isolation](#core-isolation).
- Driver and clients have to use the same Aeron directory, `/dev/shm/aeron-<user>` by default. The driver service from AeronFramework runs as `root`, so run these programs as `root` too, or set the same `AERON_DIR` for all of them.
- The subscriber runs until you stop it with Ctrl+C. The publisher exits after its 100,000,000 messages.
- Messages look like `<port>:<32 random capital letters>`, for example `40001:QWERTYUIOPASDFGHJKLZXCVBNMQWERTY`. The IPC publisher uses `IPC` as the prefix.
- The subscriber prints every message under a mutex. Redirect its output (`> /dev/null` or to a file) when you measure throughput.

## Examples

```
publisher :40001 --\
publisher :40002 ---\    Aeron subscription      mpsc::multiRingQueue
publisher :40003 ----+-> one thread per channel -> one ring per channel -> pop() on the main thread -> stdout
publisher :40004 ---/    (stream 73)
publisher (IPC)  --/
```

Run the whole setup in separate terminals:

```
# 1. media driver (or the systemd service, see AeronFramework)
aeron_driver

# 2. subscriber
./build/aeron_subscriber

# 3. publishers: [port [core]], one per channel
./build/aeron_publisher 40001 0
./build/aeron_publisher 40002 1
./build/aeron_publisher 40003 2
./build/aeron_publisher 40004 3
./build/aeron_publisher
```

`aeron_publisher [port [core]]`: without arguments it publishes on the IPC channel; with a port it publishes to `aeron:udp?endpoint=127.0.0.1:<port>`. The optional second argument pins the main thread to that core, so the IPC publisher cannot be pinned this way. Cores `0` to `3` do not clash with the subscriber (`4, 5, 9, 11, 12, 13`) or with the driver core (`6`) set by `daemonizer.sh` in AeronFramework.

The subscriber logs `[Server]` lines for each added subscription, the publisher logs `[Client]` lines for connection and progress.

## Build

Install the two dependencies first:

```
git clone https://github.com/MonsterPerformance/LockFreeQueue.git
cmake -S LockFreeQueue -B LockFreeQueue/build -DLOCKFREEQUEUE_BUILD_TESTS=OFF -DLOCKFREEQUEUE_BUILD_EXAMPLES=OFF
sudo cmake --install LockFreeQueue/build

git clone https://github.com/MonsterPerformance/AeronFramework.git
cmake -S AeronFramework -B AeronFramework/build
cmake --build AeronFramework/build -j
sudo cmake --install AeronFramework/build --prefix /usr/local
```

Then build this project:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The top-level `CMakeLists.txt` does not set a build type, so pass `Release` yourself. Binaries land in `build/`.

Requires Linux (`pthread_setaffinity_np`), CMake 3.20+ and a C++23 compiler with `<expected>` and `std::jthread` (GCC 13+).

With dependencies installed somewhere other than `/usr/local`, add `-DCMAKE_PREFIX_PATH=<prefix>`. The sources include `<aeron/Aeron.h>`, so `<prefix>/include` must also be on the compiler's include path (`-DCMAKE_CXX_FLAGS=-I<prefix>/include`). `/usr/local/include` already is.

## Core isolation

`pthread_setaffinity_np` only says where a thread may run. It does not keep other processes, kernel threads or interrupts off that core. To get truly isolated cores, add kernel parameters in the GRUB configuration.

For the default layout (subscriber on `4, 5, 9, 11, 12, 13`, driver on `6`), edit `/etc/default/grub`:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash isolcpus=4,5,6,9,11,12,13 nohz_full=4,5,6,9,11,12,13 rcu_nocbs=4,5,6,9,11,12,13"
```

- `isolcpus` removes the cores from the general scheduler, so only threads pinned to them run there.
- `nohz_full` stops the periodic scheduler tick on them while a single task is running.
- `rcu_nocbs` moves RCU callback processing to other cores.

Keep your existing parameters in that line and make sure at least one core (usually `0`) stays out of the lists for housekeeping. Apply the change and reboot:

```
sudo update-grub
sudo reboot
```

Check after the reboot:

```
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
```

If you change `mainLoopCoreID`, `brokerCoreID` or the driver's `CPUAffinity=`, update the lists to match. Isolated cores stay idle unless something is pinned to them, which is why the driver service and the subscriber must be pinned explicitly.

## Run the media driver

The programs need a running Aeron media driver. AeronFramework builds one (`aeron_driver`) and ships `daemonizer.sh`, which installs it as a systemd service. See [Run the driver as a daemon](https://github.com/MonsterPerformance/AeronFramework#run-the-driver-as-a-daemon) there.

## Layout

```
src/aeron_publisher.cpp    publisher: random messages to one channel
src/aeron_subscriber.cpp   subscriber: five channels -> mpsc::multiRingQueue -> stdout
src/aeron_common.hpp       pinThreadToCore()
CMakeLists.txt             find_package(LockFreeQueue), find_package(Aeron)
```
