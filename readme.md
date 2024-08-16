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
