const std = @import("std");
const Sha = std.crypto.hash.sha2.Sha256;
const Gcm = std.crypto.aead.aes_gcm.Aes256Gcm;
export fn portable_hash_new() ?*Sha {
    const p = std.heap.c_allocator.create(Sha) catch return null;
    p.* = Sha.init(.{});
    return p;
}
export fn portable_hash_add(ctx: *Sha, data: [*]const u8, len: usize) void {
    ctx.update(data[0..len]);
}
export fn portable_hash_end(ctx: *Sha, out: *[32]u8) void {
    ctx.final(out);
    std.crypto.utils.secureZero(u8, std.mem.asBytes(ctx));
    std.heap.c_allocator.destroy(ctx);
}
export fn portable_decrypt(key: *const [32]u8, header: *const [64]u8,
    cipher: [*]const u8, tag: *const [16]u8, plain: [*]u8, len: usize) c_int {
    Gcm.decrypt(plain[0..len], cipher[0..len], tag.*, header, header[8..20].*, key.*) catch return 0;
    return 1;
}
