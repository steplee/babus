# Babus
Efficient shared memory-based queueing system.

Allows data to be shared between multiple process and/or threads.

Provides an event-driven functionality such that a user can setup a set of channels (`Slot`s) to listen to, putting the thread to sleep until data are ready.

Built from scratch on shared memory (Linux `tmpfs`), `futex`, and atomic integers.

## Why
Most open-source queueing / message passing frameworks are built using the OS networking stack. This makes them relatively slow for passing large data, because things must be at minimum copied into the kernel, and back out. Ping-ponging a `1920x1080x3` image with redis takes around 10 ms for example, and using it's stream API things get even worse.

On the other hand efficient patterns like using condition variables and futures are clunky and do not scale to a more complicated topology than sending data from one producer to one consumer.

The data access patterns that `babus` provides can be summarized as pub/sub but also with the ability to randomly access the last `N` messages pushed to a `Slot` (the name for a channel).


## Details
The event stuff is built on `futex`, namely the `FUTEX_WAIT_BITSET` and `FUTEX_WAKE_BITSET` commands. I've written before about why `futex` is more efficient then e.g. `condition_variable`. Basically using `futex` with the bitset commands checks the condition in kernel space and wakes only those that must be, where as a similar effect with condition_variable would require waking all waiters and checking the condition -- only for most of them to go back to sleep.

One caveat is that the bitset must be only 32 bits. So we have an effective number of channels of 32. We can support more via aliasing more `Slots` to one bit of the mask, and it's not an issue in any use case of mine.

> TODO: Checkout `futex_waitv` from `futex2`.

### History
This started as an experimental project in rust. My initial thought was to make use of one shared memory file and implement an allocator. So I started on that and realized a simpler approach that might use marginally more memory would be to just mmap multiple individual shared memory files (multiple `tmpfs` files), one per slot plus one for the `Domain`. This removes the need for implementing, profiling, improving, and debugging a memory allocator. And only at the cost of *maybe* slightly more mem usage.

So I started implementing the second idea in rust. I got most of the way done but the rust code was so full of `unsafe` and rust anti-patterns that I decided to just re-implement it in C. But if you're using C you might as well use C++ to make use of RAII guards and the rest of the nice features. So my "coding goal" (in addition to actually implementing the functionality, the main goal) shifted from learning rust with a cool systems project to implementing a high-quality + modern C++ library.

### Extra Background
#### The need for `futex`
`futex` is used for event signalling and to implement a mutex.

A simple mutex could be implemented by having an atomic integer and proper logic to allow only one thread to access a critical region / shared resource at a time. But this would be require the other threads to **spin**waiting for the lock (looping + yielding, probably utilizing nearly 100% of cpu). So for all but tiny critical regions this is wasteful in cpu usage, battery, heat, etc. So the kernel provides several mechanisms for more sensible mutual exclusion. Then `pthreads` and `glibc` implement user-space functionality around these, and finally most programming language's standard libraries wrap those.

[Futex waiting works](https://elixir.bootlin.com/linux/v6.7-rc8/source/kernel/futex/waitwake.c) by placing the current task to sleep (`TASK_INTERRUPTIBLE`), adding the current task to a hash map based on `uaddr`, and scheduling the next task. Waking works by looking up `uaddr` in the hash map and waking up to as many tasks as asked to.

#### Event Signals and `Domain`


##### Mutex

The `futex` system call can be used with atomic integers to implement a mutex. If there is no contention for the mutex, the syscall is not needed during the lock operation, only atomic operations. If there is contention, we use the futex wait operation and specify the atomic integer's address as `uaddr`. Then when the thread that currently holds the mutex releases, it uses the futex wake operation. This wakes the waiter, and it retries the atomic lock and may sleep again if another thread locked before it completed the lock operation.
> NOTE: The mutex unlock operation always must call futex wake (at least in my current implementation).

##### Event Signalling
Similarly `futex` can be used for event signalling. A 32-bit sequence counter counts up and threads can wait for it to increment using futex wait. The incrementor threads must call futex wake.

## TODOs and Some Thoughts
 - Support double- and up-to 8-buffering and for the client to be able to look back in the ring buffer.
  - I believe I can keep the code simple and NOT have to deal with dynamic offsets from the mmap pointer.
  - By specifying a max data size limit, we can simply get `data_item_ptr[i] = data_item_ptr[0] + max_size`.
   - This relies on the data being aligned to OS pages (4096 bytes usually). Then because pages allow sparse access without actually taking up ram, I believe this means we waste no RAM (only virtual addresses -- who cares?).
 - C ffi bindings and a Rust and Python integration.
 - Tool/library to vizualize live messaging.

## :fire:
 - How to handle corrupt data? Imagine std::terminate being called when a mutex is held. That jacks up everything and would require a full reset of the domain + all slots.
 - In `rw_mutex.hpp` (`r_lock` and `w_lock`), see the comments about the assertions I had to disable. While they fix the breaking issues, do they also create an issue where the loop spins without sleeping properly?

## ABA Problems


# Profile
## With One Large Message Type
Here's a profile of redis streams (tcp and unix domain socket) vs babus. The same producer/consumer topology and publish rates are used, and the two communications backbones are compared. Because redis is socket based, it must copy data multiple times and call into the kernel a lot. The redis server is also single-threaded. These factors lead to babus being much faster, especially when large messages are sent around. This first profile includes one channel (`image`) being pushed to at 30 Hz with a large message (~6Mb, the size of an uncompressed full HD image).
```
*******************************************************************************************
Redis (TCP)
*******************************************************************************************
[2024-08-16 09:20:24.404] [info] [profileCfg.hpp:48] imageSize: 6220800
[2024-08-16 09:20:24.404] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:20:24.404] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:20:24.404] [info] [main.cc:318] redis using tcp: true
[2024-08-16 09:20:54.405] [info] [main.cc:194] Consumer '                                  => imu' avg latency of view + copy : 501.0us (n        18955)
[2024-08-16 09:20:54.451] [info] [main.cc:194] Consumer '                                => image' avg latency of view + copy :  31.4ms (n          487)
[2024-08-16 09:20:54.452] [info] [main.cc:194] Consumer '                          => image+med01' avg latency of view + copy :  14.0ms (n         1495)
[2024-08-16 09:20:54.452] [info] [main.cc:194] Consumer '                      => image+med0[1-5]' avg latency of view + copy :   8.7ms (n         5694)
[2024-08-16 09:20:54.452] [info] [main.cc:194] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy :   2.3ms (n        24605)
[2024-08-16 09:20:54.452] [info] [main.cc:194] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :   2.3ms (n        24606)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                  imu =>' avg latency of write       : 526.5us (n        18955)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                image =>' avg latency of write       :  24.8ms (n          487)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                med01 =>' avg latency of write       :   4.6ms (n         1009)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                med02 =>' avg latency of write       :   4.8ms (n         1024)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                med03 =>' avg latency of write       :   4.9ms (n         1040)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                med04 =>' avg latency of write       :   5.0ms (n         1057)
[2024-08-16 09:20:54.452] [info] [main.cc:128] Producer '                                med05 =>' avg latency of write       :   4.9ms (n         1082)

*******************************************************************************************
Redis (Unix Domain Sockets)
*******************************************************************************************
[2024-08-16 09:20:54.454] [info] [profileCfg.hpp:48] imageSize: 6220800
[2024-08-16 09:20:54.454] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:20:54.454] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:20:54.454] [info] [main.cc:318] redis using tcp: false
[2024-08-16 09:21:24.462] [info] [main.cc:194] Consumer '                                  => imu' avg latency of view + copy : 332.2us (n        20684)
[2024-08-16 09:21:24.474] [info] [main.cc:194] Consumer '                                => image' avg latency of view + copy :  28.5ms (n          560)
[2024-08-16 09:21:24.475] [info] [main.cc:194] Consumer '                          => image+med01' avg latency of view + copy :  12.7ms (n         1691)
[2024-08-16 09:21:24.475] [info] [main.cc:194] Consumer '                      => image+med0[1-5]' avg latency of view + copy :   7.9ms (n         6402)
[2024-08-16 09:21:24.475] [info] [main.cc:194] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy :   2.7ms (n        26615)
[2024-08-16 09:21:24.476] [info] [main.cc:194] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :   2.7ms (n        26628)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                  imu =>' avg latency of write       : 396.0us (n        20684)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                image =>' avg latency of write       :  17.2ms (n          560)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                med01 =>' avg latency of write       :   1.4ms (n         1132)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                med02 =>' avg latency of write       :   1.5ms (n         1154)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                med03 =>' avg latency of write       :   1.7ms (n         1170)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                med04 =>' avg latency of write       :   2.0ms (n         1183)
[2024-08-16 09:21:24.476] [info] [main.cc:128] Producer '                                med05 =>' avg latency of write       :   2.0ms (n         1208)

*******************************************************************************************
Babus
*******************************************************************************************
[2024-08-16 09:21:24.488] [info] [profileCfg.hpp:48] imageSize: 6220800
[2024-08-16 09:21:24.488] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:21:24.488] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:21:24.488] [info] [profileBabus.cc:209] 
[2024-08-16 09:21:24.488] [info] [profileBabus.cc:210] Running with `noImuNorMed345`=false
[2024-08-16 09:21:54.489] [info] [profileBabus.cc:163] Consumer '                                  => imu' avg latency of view        :  12.1us (n        28279)
[2024-08-16 09:21:54.489] [info] [profileBabus.cc:165] Consumer '                                  => imu' avg latency of view + copy :  12.4us (n        28279)
[2024-08-16 09:21:54.491] [info] [profileBabus.cc:163] Consumer '                                => image' avg latency of view        : 559.4us (n          814)
[2024-08-16 09:21:54.491] [info] [profileBabus.cc:165] Consumer '                                => image' avg latency of view + copy :   3.2ms (n          814)
[2024-08-16 09:21:54.491] [info] [profileBabus.cc:163] Consumer '                          => image+med01' avg latency of view        : 285.2us (n         2010)
[2024-08-16 09:21:54.491] [info] [profileBabus.cc:165] Consumer '                          => image+med01' avg latency of view + copy :   1.3ms (n         2010)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:163] Consumer '                      => image+med0[1-5]' avg latency of view        : 155.6us (n         7090)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:165] Consumer '                      => image+med0[1-5]' avg latency of view + copy : 453.4us (n         7090)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:163] Consumer '                  => imu+image+med0[1-5]' avg latency of view        :  45.2us (n        33426)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:165] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy : 109.7us (n        33426)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:163] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view        :  45.2us (n        33442)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:165] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy : 109.3us (n        33442)
[2024-08-16 09:21:54.492] [info] [profileBabus.cc:110] Producer '                                  imu =>' avg latency of write       :   3.9us (n        28287)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                image =>' avg latency of write       : 552.5us (n          815)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                med01 =>' avg latency of write       :   4.8us (n         1197)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                med02 =>' avg latency of write       :   6.1us (n         1226)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                med03 =>' avg latency of write       :   6.3us (n         1256)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                med04 =>' avg latency of write       :   5.0us (n         1286)
[2024-08-16 09:21:54.524] [info] [profileBabus.cc:110] Producer '                                med05 =>' avg latency of write       :   4.9us (n         1316)
```
## With All Small Messages
When the image size is reduced from 1920*1080*3 to just 1080, the difference in read latency narrows, but the write latency stays high with redis:
```
*******************************************************************************************
Redis (TCP)
*******************************************************************************************
[2024-08-16 09:30:16.318] [info] [profileCfg.hpp:48] imageSize: 1080
[2024-08-16 09:30:16.318] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:30:16.318] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:30:16.318] [info] [main.cc:318] redis using tcp: true
[2024-08-16 09:30:46.320] [info] [main.cc:194] Consumer '                                  => imu' avg latency of view + copy :  63.8us (n        25984)
[2024-08-16 09:30:46.328] [info] [main.cc:194] Consumer '                                => image' avg latency of view + copy : 142.6us (n          894)
[2024-08-16 09:30:46.328] [info] [main.cc:194] Consumer '                          => image+med01' avg latency of view + copy : 119.6us (n         2083)
[2024-08-16 09:30:46.328] [info] [main.cc:194] Consumer '                      => image+med0[1-5]' avg latency of view + copy : 103.6us (n         7136)
[2024-08-16 09:30:46.328] [info] [main.cc:194] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy :  65.6us (n        33119)
[2024-08-16 09:30:46.328] [info] [main.cc:194] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :  65.4us (n        33119)
[2024-08-16 09:30:46.328] [info] [main.cc:128] Producer '                                  imu =>' avg latency of write       :  98.7us (n        25984)
[2024-08-16 09:30:46.328] [info] [main.cc:128] Producer '                                image =>' avg latency of write       : 169.7us (n          894)
[2024-08-16 09:30:46.328] [info] [main.cc:128] Producer '                                med01 =>' avg latency of write       : 150.0us (n         1190)
[2024-08-16 09:30:46.338] [info] [main.cc:128] Producer '                                med02 =>' avg latency of write       : 144.7us (n         1220)
[2024-08-16 09:30:46.338] [info] [main.cc:128] Producer '                                med03 =>' avg latency of write       : 131.5us (n         1250)
[2024-08-16 09:30:46.338] [info] [main.cc:128] Producer '                                med04 =>' avg latency of write       : 142.1us (n         1279)
[2024-08-16 09:30:46.338] [info] [main.cc:128] Producer '                                med05 =>' avg latency of write       : 150.2us (n         1308)

*******************************************************************************************
Redis (Unix Domain Sockets)
*******************************************************************************************
[2024-08-16 09:30:46.339] [info] [profileCfg.hpp:48] imageSize: 1080
[2024-08-16 09:30:46.339] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:30:46.339] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:30:46.339] [info] [main.cc:318] redis using tcp: false
[2024-08-16 09:31:16.340] [info] [main.cc:194] Consumer '                                  => imu' avg latency of view + copy :  50.3us (n        26410)
[2024-08-16 09:31:16.356] [info] [main.cc:194] Consumer '                                => image' avg latency of view + copy : 115.2us (n          895)
[2024-08-16 09:31:16.356] [info] [main.cc:194] Consumer '                          => image+med01' avg latency of view + copy :  92.2us (n         2085)
[2024-08-16 09:31:16.356] [info] [main.cc:194] Consumer '                      => image+med0[1-5]' avg latency of view + copy :  72.8us (n         7143)
[2024-08-16 09:31:16.356] [info] [main.cc:194] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy :  51.2us (n        33552)
[2024-08-16 09:31:16.356] [info] [main.cc:194] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :  51.8us (n        33552)
[2024-08-16 09:31:16.356] [info] [main.cc:128] Producer '                                  imu =>' avg latency of write       :  79.9us (n        26410)
[2024-08-16 09:31:16.356] [info] [main.cc:128] Producer '                                image =>' avg latency of write       : 135.7us (n          895)
[2024-08-16 09:31:16.356] [info] [main.cc:128] Producer '                                med01 =>' avg latency of write       : 125.7us (n         1191)
[2024-08-16 09:31:16.356] [info] [main.cc:128] Producer '                                med02 =>' avg latency of write       : 114.3us (n         1221)
[2024-08-16 09:31:16.359] [info] [main.cc:128] Producer '                                med03 =>' avg latency of write       : 115.8us (n         1251)
[2024-08-16 09:31:16.359] [info] [main.cc:128] Producer '                                med04 =>' avg latency of write       : 115.0us (n         1280)
[2024-08-16 09:31:16.359] [info] [main.cc:128] Producer '                                med05 =>' avg latency of write       : 115.9us (n         1310)

*******************************************************************************************
Babus
*******************************************************************************************
[2024-08-16 09:31:16.382] [info] [profileCfg.hpp:48] imageSize: 1080
[2024-08-16 09:31:16.382] [info] [profileCfg.hpp:49] imuRate: 1000
[2024-08-16 09:31:16.382] [info] [profileCfg.hpp:52] testDuration: 30000000
[2024-08-16 09:31:16.382] [info] [profileBabus.cc:209] 
[2024-08-16 09:31:16.382] [info] [profileBabus.cc:210] Running with `noImuNorMed345`=false
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '                                  => imu' avg latency of view        :  11.1us (n        28274)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '                                  => imu' avg latency of view + copy :  11.3us (n        28274)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '                                => image' avg latency of view        :  13.2us (n          898)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '                                => image' avg latency of view + copy :  14.2us (n          898)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '                          => image+med01' avg latency of view        :  17.5us (n         2094)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '                          => image+med01' avg latency of view + copy :  18.3us (n         2094)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '                      => image+med0[1-5]' avg latency of view        :  14.2us (n         7176)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '                      => image+med0[1-5]' avg latency of view + copy :  14.7us (n         7176)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '                  => imu+image+med0[1-5]' avg latency of view        :  11.7us (n        35453)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '                  => imu+image+med0[1-5]' avg latency of view + copy :  12.0us (n        35453)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:163] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view        :  11.8us (n        35448)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:165] Consumer '    => imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :  12.1us (n        35448)
[2024-08-16 09:31:46.383] [info] [profileBabus.cc:110] Producer '                                  imu =>' avg latency of write       :   4.5us (n        28282)
[2024-08-16 09:31:46.384] [info] [profileBabus.cc:110] Producer '                                image =>' avg latency of write       :   5.9us (n          898)
[2024-08-16 09:31:46.388] [info] [profileBabus.cc:110] Producer '                                med01 =>' avg latency of write       :   5.5us (n         1197)
[2024-08-16 09:31:46.400] [info] [profileBabus.cc:110] Producer '                                med02 =>' avg latency of write       :   5.9us (n         1227)
[2024-08-16 09:31:46.400] [info] [profileBabus.cc:110] Producer '                                med03 =>' avg latency of write       :   6.5us (n         1256)
[2024-08-16 09:31:46.400] [info] [profileBabus.cc:110] Producer '                                med04 =>' avg latency of write       :   6.0us (n         1286)
[2024-08-16 09:31:46.400] [info] [profileBabus.cc:110] Producer '                                med05 =>' avg latency of write       :   5.0us (n         1317)
```
