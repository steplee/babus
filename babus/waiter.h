#pragma once

#include "detail/small_map.hpp"
#include "domain.h"

namespace babus {

    //
    // `Waiter` is a helper class to wait on a set of `Slot`s.
    //
    // NOTE: The `ClientDomain` must out-live all `Waiter`s because
    //       they hold references to `ClientSlot`s owned by the `ClientDomain`.
    //

    struct WaitTarget {
        Slot* slot_;
        std::atomic<uint32_t> lastSeq_;
        bool wakeWith_;

        // The constructor will sample the sequence counter
        WaitTarget(Slot* slot, bool wakeWith);
        WaitTarget(WaitTarget&& o);
        WaitTarget& operator=(WaitTarget&& o);

        // Reload the sequence counter from the shared `Slot` atomic.
        // If it's higher than `lastSeq_` set `lastSeq_` to it and return true.
        // If not return false
        bool checkAndUpdate();
    };

    struct Waiter {
    public:
        inline Waiter(Domain* domain)
            : domain(domain) {
        }

        // If `wakeWith` is true that means we add the `Slot`s bitmask to our wait set.
        // This is probably what you want.
        // But you may use `wakeWith=false` so that we don't wake on every message of a certain
        // type, but still receive updates to it via `forEachNewSlot` when a different `Slot` wakes us.
        void subscribeTo(Slot* slot, bool wakeWith = true);
        void unsubscribeFrom(Slot* slot);

        // Wait for the next event.
        void waitExclusive();

        // Reload all sequence counters. For any that change, execute a user callable `f`.
        // Return the number of targets that are new / were visited.
        template <class F> inline uint32_t forEachNewSlot(F&& f) {
            // waitExclusive();
            uint32_t n_updated = 0;
            for (auto& targetKv : targets_) {
                WaitTarget& tgt  = targetKv.second;
                bool tgt_updated = tgt.checkAndUpdate();
                if (tgt_updated) {
                    n_updated++;
                    f(tgt.slot_->read());
                }
            }
            return n_updated;
        }

    private:
        Domain* domain;

        // NOTE: I don't think the char* is problematic assuming Domain lifetime includes this object's.
        SmallMap<const char*, WaitTarget> targets_;
    };

}
