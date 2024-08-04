// NOTE: Opaque types mustn't be defined. But `C_LockedView` is not opaque, the struct is returned
//       (not a pointer to it).
//       It MUST be defined in rust (otherwise I guess it changes calling convention and we get
//       UB).
//
struct ClientDomain;
struct ClientSlot;

#[repr(C)]
struct C_LockedView {
    ptr: *mut std::ffi::c_void,
    len: usize,
    mtx: *mut std::ffi::c_void,
    slot: *mut std::ffi::c_void,
}

#[link(name = "babus_ffi")]
extern "C" {

    fn babus_client_domain_open_or_create(
        path: *const u8,
        targetAddr: *const u8,
    ) -> *mut ClientDomain;
    fn babus_client_domain_close(cd: *mut ClientDomain);
    fn babus_client_domain_get_slot(cd: *mut ClientDomain, name: *const u8) -> *mut ClientSlot;

    fn babus_client_slot_write(cs: *mut ClientSlot, ptr: *const u8, len: usize);
    fn babus_client_slot_read_locked_view(cs: *mut ClientSlot) -> C_LockedView;

    fn babus_locked_view_data(clv: *const C_LockedView) -> *const std::ffi::c_void;
    fn babus_locked_view_length(clv: *const C_LockedView) -> usize;
    fn babus_unlock_view(clv: *mut C_LockedView);
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::*;

    #[test]
    fn domain_openOrCreate_writeAndRead() {
        unsafe {
            let cd = babus_client_domain_open_or_create(
                "dom2\0".as_bytes().as_ptr(),
                std::ptr::null(),
            );
            let cs = babus_client_domain_get_slot(cd, "mySlot\0".as_bytes().as_ptr());

            let data0 = vec![b'h', b'e', b'l', b'l', b'o', b'\0'];
            println!(" - Writing.");
            babus_client_slot_write(cs, data0.as_ptr(), data0.len());
            println!(" - Wrote.");

            std::thread::sleep(Duration::from_millis(10));

            println!(" - Getting locked view.");
            let mut clv = babus_client_slot_read_locked_view(cs);
            println!(" - Got locked view.");
            println!(
                " - C_LockedView {{ 0x{:0x}, n={} }}",
                babus_locked_view_data(&clv) as usize,
                babus_locked_view_length(&clv)
            );
            let data1 = {
                let n = babus_locked_view_length(&clv);
                let d = babus_locked_view_data(&clv) as *mut u8;
                let mut v = vec![0u8; n];
                v.copy_from_slice(std::slice::from_raw_parts(d, n));
                v
            };

            assert_eq!(data1, data0);

            babus_unlock_view(&mut clv);

            babus_client_domain_close(cd);
        }
    }

    #[test]
    fn domain_openOrCreate_writeAndRead_multipleTimes() {
        let iterate = |i: u64| unsafe {
            let cd = babus_client_domain_open_or_create(
                "domTest2\0".as_bytes().as_ptr(),
                std::ptr::null(),
            );
            let cs = babus_client_domain_get_slot(cd, "mySlotTest2\0".as_bytes().as_ptr());

            let data0 = i;
            println!(" - Writing.");
            babus_client_slot_write(cs, std::ptr::from_ref(&data0) as *const u8, std::mem::size_of::<u64>());
            println!(" - Wrote.");

            std::thread::sleep(Duration::from_millis(10));

            println!(" - Getting locked view.");
            let mut clv = babus_client_slot_read_locked_view(cs);
            println!(" - Got locked view.");
            println!(
                " - C_LockedView {{ 0x{:0x}, n={} }}",
                babus_locked_view_data(&clv) as usize,
                babus_locked_view_length(&clv)
            );
            let data1 = unsafe {
                let n = babus_locked_view_length(&clv);
                assert_eq!(n, 8);
                *(babus_locked_view_data(&clv) as *const u64)
            };

            assert_eq!(data1, data0);

            babus_unlock_view(&mut clv);

            babus_client_domain_close(cd);
        };

        for i in 0u64..10u64 {
            iterate(i);
        }
    }

}
