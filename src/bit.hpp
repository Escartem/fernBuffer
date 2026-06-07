#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <string>

// BitWriter

class BitWriter {
public:
    BitWriter() : cur_(0), fill_(0) {}

    // Write n LSBs of val, MSB first
    void write_bits(uint32_t val, int n) {
        if (n <= 0) return;
        // Fast path: byte-aligned, writing >8 bits
        if (fill_ == 0 && n >= 8) {
            int rem = n;
            while (rem >= 8) {
                rem -= 8;
                buf_.push_back(static_cast<uint8_t>((val >> rem) & 0xFF));
            }
            if (rem) {
                cur_  = val & ((1u << rem) - 1);
                fill_ = rem;
            }
            return;
        }
        for (int i = n - 1; i >= 0; --i) {
            cur_ = (cur_ << 1) | ((val >> i) & 1);
            if (++fill_ == 8) {
                buf_.push_back(static_cast<uint8_t>(cur_));
                cur_ = fill_ = 0;
            }
        }
    }

    void write_bytes(const uint8_t* data, size_t len) {
        if (fill_ == 0) {
            buf_.insert(buf_.end(), data, data + len);
        } else {
            for (size_t i = 0; i < len; ++i) write_bits(data[i], 8);
        }
    }

    void write_bytes(const std::vector<uint8_t>& v) { write_bytes(v.data(), v.size()); }
    void write_bytes(const std::string& s) {
        write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    std::vector<uint8_t> finish() {
        if (fill_ > 0) {
            buf_.push_back(static_cast<uint8_t>(cur_ << (8 - fill_)));
            cur_ = fill_ = 0;
        }
        return buf_;
    }

private:
    std::vector<uint8_t> buf_;
    uint32_t cur_;
    int      fill_;
};

// BitReader

class BitReader {
public:
    BitReader(const uint8_t* data, size_t len)
        : data_(data), len_(len), byte_pos_(0), cur_(0), fill_(0) {}

    int read_bits(int n) {
        int val = 0;
        for (int i = 0; i < n; ++i) {
            if (fill_ == 0) {
                if (byte_pos_ >= len_) throw std::runtime_error("Unexpected end of data");
                cur_  = data_[byte_pos_++];
                fill_ = 8;
            }
            val  = (val << 1) | ((cur_ >> 7) & 1);
            cur_ = (cur_ << 1) & 0xFF;
            --fill_;
        }
        return val;
    }

    std::vector<uint8_t> read_bytes(size_t n) {
        if (fill_ == 0) {
            if (byte_pos_ + n > len_) throw std::runtime_error("Unexpected end of data");
            std::vector<uint8_t> out(data_ + byte_pos_, data_ + byte_pos_ + n);
            byte_pos_ += n;
            return out;
        }
        std::vector<uint8_t> out(n);
        for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(read_bits(8));
        return out;
    }

private:
    const uint8_t* data_;
    size_t         len_;
    size_t         byte_pos_;
    uint8_t        cur_;
    int            fill_;
};
