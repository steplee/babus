#include <gtest/gtest.h>

#include "babus/futex.hpp"
#include "babus/rw_mutex.hpp"
#include "babus/sequence_counter.hpp"

#include <thread>

using namespace babus;

TEST(Futex, WaitWake) {
    uint32_t a = 0;
    FutexView ftx { &a };

    // Waking no waiters results in zero wakes.
    EXPECT_EQ(ftx.wake(1), 0);
    EXPECT_EQ(ftx.wake(2), 0);

    usleep(50);

    // Waking one waiter results in one wake.
    {
        std::thread t1([&]() { ftx.wait(0); });
        usleep(1'000);
        EXPECT_EQ(ftx.wake(1), 1);
        t1.join();
    }

    usleep(50);

    // Waking two waiters results in two wakes.
    {
        std::thread t1([&]() { ftx.wait(0); });
        std::thread t2([&]() { ftx.wait(0); });
        usleep(1'000);
        EXPECT_EQ(ftx.wake(2), 2);
        t1.join();
        t2.join();
    }

    usleep(50);

    // Waking two waiters REQUIRES two wakes.
    // The third wake wakes none.
    {
        std::thread t1([&]() { ftx.wait(0); });
        std::thread t2([&]() { ftx.wait(0); });
        usleep(1'000);
        EXPECT_EQ(ftx.wake(1), 1);
        usleep(1'000);
        EXPECT_EQ(ftx.wake(1), 1);
        t1.join();
        t2.join();
        EXPECT_EQ(ftx.wake(1), 0);
    }

    usleep(50);
}

TEST(RwMutex, WriterBlocksReader) {
    // The reader thread cannot make any reads because a writer is locking.
    std::atomic<int> n_reads = 0;
    std::atomic<bool> stop   = false;

    RwMutex m;
    m.w_lock();
    std::thread t1([&]() {
        while (1) {
            RwMutexReadLockGuard lck(m);
            if (stop.load()) break;
            n_reads++;
            usleep(100);
        }
    });

    usleep(25'000);
    m.w_unlock();
    stop = true;
    usleep(100);

    t1.join();
    EXPECT_EQ(n_reads.load(), 0);
}

TEST(RwMutex, ReaderDoesNotBlockReader) {
    std::atomic<int> n_reads = 0;
    std::atomic<bool> stop   = false;

    RwMutex m;
    m.r_lock();
    std::thread t1([&]() {
        while (1) {
            RwMutexReadLockGuard lck(m);
            if (stop.load()) break;
            n_reads++;
            usleep(100);
        }
    });

    usleep(25'000);
    m.r_unlock();
    stop = true;
    usleep(100);

    t1.join();
    // SPDLOG_INFO("n_reads {}", n_reads.load());
    EXPECT_GT(n_reads.load(), 0);
}

TEST(RwMutex, ReleasedWriterDoesNotBlockReader) {
    std::atomic<int> n_reads = 0;
    std::atomic<bool> stop   = false;

    RwMutex m;
    m.w_lock();
    std::thread t1([&]() {
        while (1) {
            RwMutexReadLockGuard lck(m);
            if (stop.load()) break;
            n_reads++;
            usleep(100);
        }
    });

    usleep(10'000);
    m.w_unlock();
    usleep(10'000);
    stop = true;
    usleep(100);

    t1.join();
    // SPDLOG_INFO("n_reads {}", n_reads.load());
    EXPECT_GT(n_reads.load(), 0);
}

TEST(SequenceCounter, KeepsTrack) {
    SequenceCounter sc;
    static constexpr int N = 100'000;
    std::thread t1([&]() {
        for (int i = 0; i < N; i++) sc.incrementNoFutexWake();
    });
    std::thread t2([&]() {
        for (int i = 0; i < N; i++) sc.incrementNoFutexWake();
    });
    std::thread t3([&]() {
        for (int i = 0; i < N; i++) sc.incrementNoFutexWake();
    });
    std::thread t4([&]() {
        for (int i = 0; i < N; i++) sc.incrementNoFutexWake();
    });
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    EXPECT_EQ(sc.load(), N * 4);
}
