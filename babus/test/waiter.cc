#include <gtest/gtest.h>

#include "babus/waiter.h"

#include <thread>
#include <unistd.h>
#include <sys/wait.h>

using namespace babus;

namespace {
	// Simpler than setting up with ClientDomain + mmaps and all of that.
	Domain* malloc_domain() {
		void* p = malloc(DomainFileSize);
		new (p) Domain{};
		return (Domain*) p;
	}
	Slot* malloc_slot() {
		void* p = malloc(SlotFileSize);
		new (p) Slot{};
		return (Slot*) p;
	}
}

TEST(Waiter, WaiterWorksWithJustTwoThreads) {
	
    spdlog::set_level(spdlog::level::trace);

	Domain* domain = malloc_domain();
	Slot* slot = malloc_slot();

	std::thread t([&]() {
		Waiter waiter(domain);
		waiter.subscribeTo(slot, true);

		waiter.waitExclusive();
		waiter.forEachNewSlot([](LockedView&& view) {
				if (view.slot == nullptr)
					SPDLOG_INFO("new data from unknown slot.");
				else
					SPDLOG_INFO("new data from slot: {}", *view.slot);
		});
	});

	usleep(5'000);

	const char hello[] = "hello1\0";
	slot->write(domain, {(void*)hello, 7});

	t.join();

	delete slot;
	delete domain;

}

	
TEST(Waiter, WaiterWorksAcrossTwoProcesses_SameTargetAddress) {
    spdlog::set_level(spdlog::level::trace);

	int stat = fork();

	EXPECT_GT(stat, -1);

	if (stat == 0) {
		// Child

		spdlog::set_level(spdlog::level::trace);

		ClientDomain domain = ClientDomain::openOrCreate(std::string{Prefix} + "dom2", DomainFileSize, (void*)0x714110dfc000);
		SPDLOG_INFO("child  domain    @ 0x{:0x}", (std::size_t)domain.ptr());
		SPDLOG_INFO("child  globalSeq @ 0x{:0x}", (std::size_t)&domain.ptr()->seq);
		ClientSlot& slot { domain.getSlot("mySlot") };

		Waiter waiter(domain.ptr());
		waiter.subscribeTo(slot.ptr(), true);

		SPDLOG_INFO("child calling waitExclusive()");
		waiter.waitExclusive();
		SPDLOG_INFO("child waitExclusive() returned.");
		waiter.forEachNewSlot([](LockedView&& view) {
				if (view.slot == nullptr)
					SPDLOG_INFO("new data from unknown slot.");
				else
					SPDLOG_INFO("new data from slot: {}", *view.slot);
		});


	} else {
		// Parent.

		ClientDomain domain = ClientDomain::openOrCreate(std::string{Prefix} + "dom2", DomainFileSize, (void*)0x714110dfc000);
		SPDLOG_INFO("parent domain    @ 0x{:0x}", (std::size_t)domain.ptr());
		SPDLOG_INFO("parent globalSeq @ 0x{:0x}", (std::size_t)&domain.ptr()->seq);
		ClientSlot& slot { domain.getSlot("mySlot") };

		// Wait a good amount of time so child process has time to get setup.
		usleep(100'000);

		SPDLOG_INFO("Parent writing to slot...");
		const char hello[] = "hello1\0";
		slot.write({(void*)hello, 7});

		SPDLOG_INFO("Parent waiting for child to exit...");
		int wstat;
		waitpid(stat, &wstat, 0);

	}
}

TEST(Waiter, WaiterWorksAcrossTwoProcesses_NoConsensusTargetAddress) {
    spdlog::set_level(spdlog::level::trace);

	int stat = fork();

	EXPECT_GT(stat, -1);

	if (stat == 0) {
		// Child

		spdlog::set_level(spdlog::level::trace);

		ClientDomain domain = ClientDomain::openOrCreate(std::string{Prefix} + "dom2", DomainFileSize);
		SPDLOG_INFO("child  domain    @ 0x{:0x}", (std::size_t)domain.ptr());
		SPDLOG_INFO("child  globalSeq @ 0x{:0x}", (std::size_t)&domain.ptr()->seq);
		ClientSlot& slot { domain.getSlot("mySlot") };

		Waiter waiter(domain.ptr());
		waiter.subscribeTo(slot.ptr(), true);

		SPDLOG_INFO("child calling waitExclusive()");
		waiter.waitExclusive();
		SPDLOG_INFO("child waitExclusive() returned.");
		waiter.forEachNewSlot([](LockedView&& view) {
				if (view.slot == nullptr)
					SPDLOG_INFO("new data from unknown slot.");
				else
					SPDLOG_INFO("new data from slot: {}", *view.slot);
		});


	} else {
		// Parent.

		ClientDomain domain = ClientDomain::openOrCreate(std::string{Prefix} + "dom2", DomainFileSize);
		SPDLOG_INFO("parent domain    @ 0x{:0x}", (std::size_t)domain.ptr());
		SPDLOG_INFO("parent globalSeq @ 0x{:0x}", (std::size_t)&domain.ptr()->seq);
		ClientSlot& slot { domain.getSlot("mySlot") };

		// Wait a good amount of time so child process has time to get setup.
		usleep(100'000);

		SPDLOG_INFO("Parent writing to slot...");
		const char hello[] = "hello1\0";
		slot.write({(void*)hello, 7});

		SPDLOG_INFO("Parent waiting for child to exit...");
		int wstat;
		waitpid(stat, &wstat, 0);

	}



}
