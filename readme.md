# Babus
Efficient shared memory-based queueing system.

Allows data to be shared between multiple process and/or threads.

Provides an event-driven functionality such that a user can setup a set of channels (`Slot`s) to listen to, putting the thread to sleep until data are ready.

Based on shared memory (Linux `tmpfs`), `futex`, and atomic integers.

## Why
Most open-source queueing / message passing frameworks are built using the OS networking stack. This makes them realtively slow for passing large data, because things must be at minimum copied into the kernel, and back out. Ping-ponging a `1920x1080x3` image with redis takes around 10 ms for example, and using it's stream API things get even worse.

On the other hand efficient patterns like using condition variables and futures are clunky and do not scale to a more complicated topology than sending data from one producer to one consumer.

The API that `babus` provides can be summarized as pubsub but also with the ability to randomly read the last `N` messages pushed to a `Slot` (the name for a channel).


## Details
The event stuff is built on `futex`, namely the `FUTEX_WAIT_BITSET` and `FUTEX_WAKE_BITSET` commands. I've written before about why `futex` is more efficient then e.g. `condition_variable`. Basically using `futex` with the bitset commands checks the condition in kernel space and wakes only those that must be, where as a similar effect with condition_variable would require waking all waiters and checking the condition -- only for most of them to go back to sleep.

One caveat is that the bitset must be only 32 bits. So we have an effective number of channels of 32. We can support more via aliasing more `Slots` to one channel, and it's not an issue in any use case of mine.

### History
This started as an experimental project in rust. My initial thought was to make use of one shared memory file and implement an allocator. So I started on that and realized a simpler approach that might use marginally more memory would be to just mmap multiple individual shared memory files (multiple `tmpfs` files). This removes the need for implementing, profiling, improving, and debugging a memory allocator. And only at the cost of *maybe* slightly more mem usage.

So I started implementing the second idea in rust. I got most of the way done but the rust code was so full of `unsafe` and rust anti-patterns that I decided to just re-implement it in C. But if you're using C you might as well use C++ to make use of RAII guards and the rest of the nice features. So my "coding goal" (in addition to actually implementing the functionality, the main goal) shifted from learning rust with a cool systems project to implementing a high-quality + modern C++ library.
