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
When the image size is reduced from `1920*1080*3` to just `1080`, the difference in read latency narrows, but the write latency stays high with redis:
```
*******************************************************************************************
Redis (TCP)
*******************************************************************************************
[2024-08-20 18:44:19.476] [info] [profileCfg.hpp:57] imageSize: 6220800
[2024-08-20 18:44:19.476] [info] [profileCfg.hpp:58] imuRate: 1000
[2024-08-20 18:44:19.476] [info] [profileCfg.hpp:61] testDuration: 30000000
[2024-08-20 18:44:19.476] [info] [main.cc:333] redis using tcp: true
[2024-08-20 18:44:49.477] [info] [main.cc:202] Consumer '                                     imu' avg latency of view + copy : 507.8us (n        18681)
[2024-08-20 18:44:49.483] [info] [main.cc:202] Consumer '                                   image' avg latency of view + copy :  35.9ms (n          441)
[2024-08-20 18:44:49.484] [info] [main.cc:202] Consumer '                             image+med01' avg latency of view + copy :  14.1ms (n         1462)
[2024-08-20 18:44:49.484] [info] [main.cc:202] Consumer '                         image+med0[1-5]' avg latency of view + copy :   7.1ms (n         5887)
[2024-08-20 18:44:49.484] [info] [main.cc:202] Consumer '                     imu+image+med0[1-5]' avg latency of view + copy :   2.0ms (n        24518)
[2024-08-20 18:44:49.485] [info] [main.cc:202] Consumer '       imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :   2.0ms (n        24538)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                     imu' avg latency of write       : 550.6us (n        18681)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   image' avg latency of write       :  29.5ms (n          441)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   med01' avg latency of write       :   4.3ms (n         1022)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   med02' avg latency of write       :   3.9ms (n         1056)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   med03' avg latency of write       :   3.6ms (n         1093)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   med04' avg latency of write       :   3.2ms (n         1129)
[2024-08-20 18:44:49.485] [info] [main.cc:130] Producer '                                   med05' avg latency of write       :   3.3ms (n         1151)

*******************************************************************************************
Redis (Unix Domain Sockets)
*******************************************************************************************
[2024-08-20 18:44:49.487] [info] [profileCfg.hpp:57] imageSize: 6220800
[2024-08-20 18:44:49.487] [info] [profileCfg.hpp:58] imuRate: 1000
[2024-08-20 18:44:49.487] [info] [profileCfg.hpp:61] testDuration: 30000000
[2024-08-20 18:44:49.487] [info] [main.cc:333] redis using tcp: false
[2024-08-20 18:45:19.487] [info] [main.cc:202] Consumer '                                     imu' avg latency of view + copy : 419.8us (n        19048)
[2024-08-20 18:45:19.518] [info] [main.cc:202] Consumer '                                   image' avg latency of view + copy :  32.6ms (n          543)
[2024-08-20 18:45:19.518] [info] [main.cc:202] Consumer '                             image+med01' avg latency of view + copy :  15.9ms (n         1646)
[2024-08-20 18:45:19.518] [info] [main.cc:202] Consumer '                         image+med0[1-5]' avg latency of view + copy :  10.7ms (n         6163)
[2024-08-20 18:45:19.518] [info] [main.cc:202] Consumer '                     imu+image+med0[1-5]' avg latency of view + copy :   3.5ms (n        24749)
[2024-08-20 18:45:19.518] [info] [main.cc:202] Consumer '       imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy :   3.5ms (n        24822)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                     imu' avg latency of write       : 521.2us (n        19048)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   image' avg latency of write       :  19.2ms (n          543)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   med01' avg latency of write       :   2.1ms (n         1104)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   med02' avg latency of write       :   2.5ms (n         1115)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   med03' avg latency of write       :   2.8ms (n         1123)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   med04' avg latency of write       :   3.1ms (n         1137)
[2024-08-20 18:45:19.518] [info] [main.cc:130] Producer '                                   med05' avg latency of write       :   3.4ms (n         1146)

*******************************************************************************************
Babus
*******************************************************************************************
[2024-08-20 18:45:19.521] [info] [profileCfg.hpp:57] imageSize: 6220800
[2024-08-20 18:45:19.521] [info] [profileCfg.hpp:58] imuRate: 1000
[2024-08-20 18:45:19.521] [info] [profileCfg.hpp:61] testDuration: 30000000
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '                                     imu' avg latency of view        :  17.4us (n        28181)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '                                     imu' avg latency of view + copy :  17.8us (n        28181)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '                                   image' avg latency of view        : 727.1us (n          779)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '                                   image' avg latency of view + copy :   3.8ms (n          779)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '                             image+med01' avg latency of view        : 379.7us (n         1975)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '                             image+med01' avg latency of view + copy :   1.6ms (n         1975)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '                         image+med0[1-5]' avg latency of view        : 204.6us (n         7056)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '                         image+med0[1-5]' avg latency of view + copy : 533.6us (n         7056)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '                     imu+image+med0[1-5]' avg latency of view        :  59.0us (n        33079)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '                     imu+image+med0[1-5]' avg latency of view + copy : 130.7us (n        33079)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:171] Consumer '       imu+image+med0[1-5] & sleep(10ms)' avg latency of view        :  57.9us (n        33074)
[2024-08-20 18:45:49.521] [info] [profileBabus.cc:173] Consumer '       imu+image+med0[1-5] & sleep(10ms)' avg latency of view + copy : 130.2us (n        33074)
[2024-08-20 18:45:49.522] [info] [profileBabus.cc:112] Producer '                                     imu' avg latency of write       :   6.5us (n        28181)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   image' avg latency of write       : 723.6us (n          779)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   med01' avg latency of write       :   8.1us (n         1197)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   med02' avg latency of write       :   9.2us (n         1226)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   med03' avg latency of write       :   8.0us (n         1257)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   med04' avg latency of write       :   9.0us (n         1286)
[2024-08-20 18:45:49.543] [info] [profileBabus.cc:112] Producer '                                   med05' avg latency of write       :   8.7us (n         1316)
```
