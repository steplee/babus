use crate::ffi;
use crate::ffi::{C_LockedView};

struct ClientDomain {
    domain: *mut ffi::ClientDomain,
}

struct ClientSlot {
    slot: *mut ffi::ClientSlot,
}

struct LockedView(C_LockedView);

// struct Waiter

impl Drop for LockedView {
    fn drop(&mut self) {
        let clv = std::ptr::from_mut(&mut self.0);
        unsafe {
            ffi::babus_unlock_view(clv);
        }
    }
}

impl ffi::C_LockedView {

    // Consume and return copied bytes.
    pub fn copy_to_vec(self) -> Vec<u8> {
        let clv = std::ptr::from_ref(&self);
        unsafe {
            let n = ffi::babus_locked_view_length(clv);
            let d = ffi::babus_locked_view_data(clv) as *mut u8;
            let mut v = vec![0u8; n];
            v.copy_from_slice(std::slice::from_raw_parts(d, n));
            v
        }
    }

}

impl ClientDomain {
    pub fn open_or_create(name: &str) -> Self {
        let name = ffi::rust_str_to_cstr(name);
        let name = name.as_bytes().as_ptr();

        unsafe {
            let cd = ffi::babus_client_domain_open_or_create(
                name,
                std::ptr::null(),
            );
            return Self { domain: cd };
        }
    }

    pub fn get_slot(&mut self, name: &str) -> ClientSlot {
        let name = ffi::rust_str_to_cstr(name);
        let name = name.as_bytes().as_ptr();

        return unsafe {
            ClientSlot {
                slot: ffi::babus_client_domain_get_slot(self.domain, name)
            }
        }
    }
}
impl Drop for ClientDomain {
    fn drop(&mut self) {
        unsafe {
            ffi::babus_client_domain_close(self.domain);
        }
    }
}

impl ClientSlot {
    // pub fn babus_client_slot_write(cs: *mut ClientSlot, ptr: *const u8, len: usize);
    // pub fn babus_client_slot_read_locked_view(cs: *mut ClientSlot) -> C_LockedView;

    pub fn write(&self, data: &[u8]) {
        unsafe {
            ffi::babus_client_slot_write(self.slot, data.as_ptr(), data.len());
        }
    }

    pub fn read(&self) -> C_LockedView {
        unsafe {
            ffi::babus_client_slot_read_locked_view(self.slot)
        }
    }
}

/*
impl Drop for ClientSlot {
    fn drop(&mut self) {
        todo!()
    }
}
*/

