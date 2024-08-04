#include "client.h"

using namespace babus;

extern "C" {

struct C_LockedView {
    void* ptr;
    size_t len;
    RwMutex* mtx;
    Slot* slot;
};

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
	SPDLOG_INFO("unlock C_LockedView 0x{:0x}", (size_t)clv);
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


}
