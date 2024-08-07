// NOTE: Opaque types mustn't be defined. But `C_LockedView` is not opaque, the struct is returned
//       (not a pointer to it).
//       It MUST be defined in rust (otherwise I guess it changes calling convention and we get
//       UB).
//
pub struct ClientDomain;
pub struct ClientSlot;
pub struct Waiter;

pub fn rust_str_to_cstr(s: &str) -> String {
    let mut out = s.to_owned();
    out.push_str("\0");
    return out;
}

#[repr(C)]
pub struct C_LockedView {
    ptr: *mut std::ffi::c_void,
    len: usize,
    mtx: *mut std::ffi::c_void,
    slot: *mut std::ffi::c_void,
}

type ForEachNewSlotCallback = extern "C" fn(C_LockedView, *mut std::ffi::c_void) -> ();

#[link(name = "babus_ffi")]
extern "C" {

    pub fn babus_client_domain_open_or_create(
        path: *const u8,
        targetAddr: *const u8,
    ) -> *mut ClientDomain;
    pub fn babus_client_domain_close(cd: *mut ClientDomain);
    pub fn babus_client_domain_get_slot(cd: *mut ClientDomain, name: *const u8) -> *mut ClientSlot;

    pub fn babus_client_slot_write(cs: *mut ClientSlot, ptr: *const u8, len: usize);
    pub fn babus_client_slot_read_locked_view(cs: *mut ClientSlot) -> C_LockedView;

    pub fn babus_locked_view_data(clv: *const C_LockedView) -> *const std::ffi::c_void;
    pub fn babus_locked_view_length(clv: *const C_LockedView) -> usize;
    pub fn babus_unlock_view(clv: *mut C_LockedView);

    pub fn babus_waiter_alloc(cd: *mut ClientDomain) -> *mut Waiter;
    pub fn babus_waiter_free(w: *mut Waiter);
    pub fn babus_waiter_subscribe_to(w: *mut Waiter, cs: *mut ClientSlot, wakeWith: bool);
    pub fn babus_waiter_unsubscribe_from(w: *mut Waiter, cs: *mut ClientSlot);
    pub fn babus_waiter_wait_exclusive(w: *mut Waiter);

    pub fn babus_waiter_for_each_new_slot(w: *mut Waiter, userData: *mut std::ffi::c_void, cb: ForEachNewSlotCallback);

}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::*;

    #[test]
    fn domain_openOrCreate_writeAndRead_once() {

        assert_eq!(std::mem::size_of::<C_LockedView>(), 4*8);

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

    // Fulfills the contract of `ForEachNewSlotCallback`.
    // Casts `user_data` as a *u64 and dereferences it.
    // Reads the LockedView also as a u64.
    // Asserts these two integers are the same.
    extern "C" fn my_print_callback(mut clv: C_LockedView, user_data: *mut std::ffi::c_void) {
        unsafe {

            let data1 = {
                let n = babus_locked_view_length(&clv);
                assert_eq!(n, 8);
                *(babus_locked_view_data(&clv) as *const u64)
            };

            let expected = {
                *(user_data as *const u64)
            };

            println!("waiter.forEachNewSlot got value {}, expected {}", data1, expected);

            babus_unlock_view(&mut clv);
        }
    }

    #[test]
    fn domain_waiter() {

        let writer_thread = std::thread::spawn(|| unsafe {

            std::thread::sleep(Duration::from_millis(100));

            let cd = babus_client_domain_open_or_create(
                "domTest3\0".as_bytes().as_ptr(),
                std::ptr::null(),
            );
            let cs = babus_client_domain_get_slot(cd, "mySlotTest3\0".as_bytes().as_ptr());

            for i in 0u64..10u64 {
                let data0 = i;
                println!(" - Writing {}.", i);
                babus_client_slot_write(cs, std::ptr::from_ref(&data0) as *const u8, std::mem::size_of::<u64>());
                println!(" - Wrote.");

                std::thread::sleep(Duration::from_millis(10));
            }

            babus_client_domain_close(cd);
        });

        let reader_thread = std::thread::spawn(|| unsafe {
            let cd = babus_client_domain_open_or_create(
                "domTest3\0".as_bytes().as_ptr(),
                std::ptr::null(),
            );
            let cs = babus_client_domain_get_slot(cd, "mySlotTest3\0".as_bytes().as_ptr());

            let w = babus_waiter_alloc(cd);
            babus_waiter_subscribe_to(w, cs, true);

            for i in 0u64..10u64 {
                babus_waiter_wait_exclusive(w);
                println!("waiter awoke.");
                // babus_waiter_for_each_new_slot(w, std::ptr::null_mut(), my_print_callback);
                babus_waiter_for_each_new_slot(w, std::ptr::from_ref(&i) as *mut std::ffi::c_void, my_print_callback);

            }

            babus_waiter_free(w);
        });

        writer_thread.join().unwrap();
        reader_thread.join().unwrap();

    }

}

