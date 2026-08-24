#pragma once
// Little-endian binary blob reader/writer used by the save system.
// The reader never trusts its input: every read is bounds-checked and sets a
// failure flag instead of throwing or reading out of range.
#include "core/Types.hpp"
#include "core/Math.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace hv {

class BlobWriter {
public:
    void u8v(u8 v)   { raw(&v, 1); }
    void u16v(u16 v) { writeLE(v); }
    void u32v(u32 v) { writeLE(v); }
    void u64v(u64 v) { writeLE(v); }
    void i32v(i32 v) { writeLE(static_cast<u32>(v)); }
    void boolv(bool v) { u8v(v ? 1 : 0); }
    void f32v(f32 v) { u32 bits; std::memcpy(&bits, &v, 4); writeLE(bits); }
    void f64v(f64 v) { u64 bits; std::memcpy(&bits, &v, 8); writeLE(bits); }
    void str(const std::string& s) {
        u32v(static_cast<u32>(s.size()));
        raw(s.data(), s.size());
    }
    void vec3(const Vec3& v) { f32v(v.x); f32v(v.y); f32v(v.z); }
    void raw(const void* p, size_t n) {
        const u8* b = static_cast<const u8*>(p);
        data_.insert(data_.end(), b, b + n);
    }
    const std::vector<u8>& data() const { return data_; }
    std::vector<u8>&& take() { return std::move(data_); }
    size_t size() const { return data_.size(); }

private:
    template <typename T>
    void writeLE(T v) {
        u8 tmp[sizeof(T)];
        for (size_t i = 0; i < sizeof(T); ++i) tmp[i] = static_cast<u8>((v >> (8 * i)) & 0xFF);
        raw(tmp, sizeof(T));
    }
    std::vector<u8> data_;
};

class BlobReader {
public:
    BlobReader(const u8* data, size_t size) : data_(data), size_(size) {}
    explicit BlobReader(const std::vector<u8>& v) : data_(v.data()), size_(v.size()) {}

    u8   u8v()   { u8 v = 0; readRaw(&v, 1); return v; }
    u16  u16v()  { return readLE<u16>(); }
    u32  u32v()  { return readLE<u32>(); }
    u64  u64v()  { return readLE<u64>(); }
    i32  i32v()  { return static_cast<i32>(readLE<u32>()); }
    bool boolv() { return u8v() != 0; }
    f32  f32v()  { const u32 b = readLE<u32>(); f32 v; std::memcpy(&v, &b, 4); return v; }
    f64  f64v()  { const u64 b = readLE<u64>(); f64 v; std::memcpy(&v, &b, 8); return v; }
    Vec3 vec3()  { Vec3 v; v.x = f32v(); v.y = f32v(); v.z = f32v(); return v; }

    std::string str() {
        const u32 n = u32v();
        // A corrupted length must not make us allocate gigabytes.
        if (failed_ || n > size_ - cursor_) { failed_ = true; return {}; }
        std::string s(reinterpret_cast<const char*>(data_ + cursor_), n);
        cursor_ += n;
        return s;
    }
    void skip(size_t n) {
        if (cursor_ + n > size_) { failed_ = true; cursor_ = size_; return; }
        cursor_ += n;
    }
    bool failed() const { return failed_; }
    bool exhausted() const { return cursor_ >= size_; }
    size_t remaining() const { return size_ - cursor_; }
    size_t cursor() const { return cursor_; }

private:
    void readRaw(void* out, size_t n) {
        if (failed_ || cursor_ + n > size_) { failed_ = true; std::memset(out, 0, n); return; }
        std::memcpy(out, data_ + cursor_, n);
        cursor_ += n;
    }
    template <typename T>
    T readLE() {
        u8 tmp[sizeof(T)];
        readRaw(tmp, sizeof(T));
        T v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v |= static_cast<T>(tmp[i]) << (8 * i);
        return v;
    }
    const u8* data_;
    size_t    size_;
    size_t    cursor_ = 0;
    bool      failed_ = false;
};

/// CRC-32 (IEEE) over a buffer — the save format's corruption check.
u32 crc32(const u8* data, size_t size);

} // namespace hv
