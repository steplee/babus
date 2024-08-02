#include "waiter.h"

namespace babus {

		WaitTarget::WaitTarget(Slot* slot, bool wakeWith) : slot_(slot), wakeWith_(wakeWith) {
			lastSeq_ = slot->seq.load();
		}

		WaitTarget::WaitTarget(WaitTarget&& o) {
			slot_ = o.slot_;
			lastSeq_ = o.lastSeq_.load();
			wakeWith_ = o.wakeWith_;
		}
		WaitTarget& WaitTarget::operator=(WaitTarget&& o) {
			slot_ = o.slot_;
			lastSeq_ = o.lastSeq_.load();
			wakeWith_ = o.wakeWith_;
			return *this;
		}

		bool WaitTarget::checkAndUpdate() {
			auto newValue = slot_->seq.load();
			if (newValue > lastSeq_) {
				lastSeq_ = newValue;
				return true;
			}
			return false;
		}

		void Waiter::subscribeTo(Slot* slot, bool wakeWith) {
			targets_.insert(slot->name, WaitTarget{slot, wakeWith});
		}

		void Waiter::unsubscribeFrom(Slot* slot) {
			targets_.erase(slot->name);
		}

		void Waiter::waitExclusive() {
			assert(targets_.size() > 0);

			uint32_t mask = 0;
			for (const auto& kv : targets_) mask |= (1u << kv.second.slot_->index);

			assert(domain != nullptr);
			uint32_t prv = domain->seq.load();
			SPDLOG_TRACE("waitExclusive (global prv {}), waiting now on mask {}.", prv, mask);
			domain->seq.waitForChange(prv, mask);

		}

}
