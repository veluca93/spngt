#[no_mangle]
pub extern "C" fn get_name() -> *const u8 {
    "rust\0".as_ptr()
}

#[no_mangle]
pub unsafe extern "C" fn decode_png(
    data: *const u8,
    size: u64,
    w: *mut u32,
    h: *mut u32,
) -> *mut u8 {
    let data = std::slice::from_raw_parts(data, size as usize);
    let reader = std::io::Cursor::new(data);
    let mut decoder = png::Decoder::new(reader);
    decoder.set_transformations(png::Transformations::EXPAND);
    let mut reader = decoder.read_info().unwrap();
    // Allocate the output buffer.
    let mut buf = vec![0; reader.output_buffer_size()];
    // Read the next frame. An APNG might contain multiple frames.
    let info = reader.next_frame(&mut buf).unwrap();
    *w = info.width;
    *h = info.height;
    let ptr = buf.as_mut_ptr();
    std::mem::forget(buf);
    ptr
}
