#include "client.h"
#include "waiter.h"

using namespace babus;

extern "C" {

struct C_LockedView {
    void* ptr;
    size_t len;
    RwMutex* mtx;
    Slot* slot;
};
static_assert(sizeof(C_LockedView) == 4*8);

// -----------------------------------------------------
// ClientDomain
// -----------------------------------------------------

ClientDomain* babus_client_domain_open_or_create(const char* path, void* targetAddr) {

	// FIXME: Remove this.
    spdlog::set_level(spdlog::level::trace);

    return new ClientDomain(ClientDomain::openOrCreate(path, DomainFileSize, targetAddr));
}
void babus_client_domain_close(ClientDomain* cd) {
	SPDLOG_INFO("delete ClientDomain 0x{:0x}", (size_t)cd);
    delete cd;
}

ClientSlot* babus_client_domain_get_slot(ClientDomain* cd, const char* name) {
    return &cd->getSlot(name);
}

// -----------------------------------------------------
// ClientSlot
// -----------------------------------------------------

void babus_client_slot_write(ClientSlot* cs, void* ptr, size_t len) {
    cs->write(ByteSpan { ptr, len });
}

C_LockedView babus_client_slot_read_locked_view(ClientSlot* cs) {
    C_LockedView clv;
	// SPDLOG_INFO("read ClientSlot @ 0x{:0x}", (size_t)cs);
    auto lv  = cs->read();

    clv.ptr  = lv.span.ptr;
    clv.len  = lv.span.len;
    clv.mtx  = lv.lck.forgetUnsafe();
    clv.slot = lv.slot;

    return clv;
}

// -----------------------------------------------------
// C_LockedView
// -----------------------------------------------------

void babus_unlock_view(C_LockedView* clv) {
	SPDLOG_TRACE("unlock C_LockedView 0x{:0x}", (size_t)clv);
    assert(clv != nullptr);

    assert(clv->mtx != nullptr);
    // if (clv->mtx)
    clv->mtx->r_unlock();
}

void* babus_locked_view_data(C_LockedView* clv) {
	return clv->ptr;
}
size_t babus_locked_view_length(C_LockedView* clv) {
	return clv->len;
}

// -----------------------------------------------------
// Waiter
// -----------------------------------------------------

Waiter* babus_waiter_alloc(ClientDomain* cd) {
	return new Waiter(cd->ptr());
}
void babus_waiter_free(Waiter* w) {
	free(w);
}

void babus_waiter_subscribe_to(Waiter* waiter, ClientSlot* cs, bool wakeWith) {
	waiter->subscribeTo(cs->ptr(), wakeWith);
}
void babus_waiter_unsubscribe_from(Waiter* waiter, ClientSlot* cs) {
	waiter->unsubscribeFrom(cs->ptr());
}
void babus_waiter_wait_exclusive(Waiter* waiter) {
	waiter->waitExclusive();
}

// The user must pass a function pointer that takes C_LockedView and an arbirtray pointer that they may or may not make use of.
using ForEachNewSlotCallback = void (*)(C_LockedView, void*);

uint32_t babus_waiter_for_each_new_slot(Waiter* waiter, void* userData, ForEachNewSlotCallback callback) {
	return waiter->forEachNewSlot([=](LockedView&& lv) {
			C_LockedView clv;
			clv.ptr  = lv.span.ptr;
			clv.len  = lv.span.len;
			clv.mtx  = lv.lck.forgetUnsafe();
			clv.slot = lv.slot;
			callback(clv, userData);
	});
}


}
