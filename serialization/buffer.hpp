#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <stdio.h>
#include <string>
#include <vector>

namespace serialization {
    uint64_t htonll(uint64_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint64_t) htonl(value & 0xFFFFFFFF) << 32) |
               htonl(value >> 32);
#else
        return value;
#endif
    }

    uint64_t ntohll(uint64_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        return ((uint64_t) ntohl(value & 0xFFFFFFFF) << 32) |
               ntohl(value >> 32);
#else
        return value;
#endif
    }

    class BufferWriter {
    private:
        void write_bytes(const void *data, size_t size) {
            size_t offset = buffer_.size();
            buffer_.resize(offset + size);
            memcpy(buffer_.data(), data, size);
        }

    public:
        std::vector<char> buffer_;

        BufferWriter() : buffer_{} {}

        void write_u8(uint8_t val) {
            buffer_.push_back(static_cast<char>(val));
        }

        void write_u16(uint16_t val) {
            uint16_t encoded = htons(val);
            write_bytes(&encoded, sizeof(encoded));
        }

        void write_u32(uint32_t val) {
            uint32_t encoded = htonl(val);
            write_bytes(&encoded, sizeof(encoded));
        }

        void write_u64(uint64_t val) {
            uint64_t encoded = htonll(val);
            write_bytes(&encoded, sizeof(encoded));
        }

        void write_string(const std::string &s) {
            write_u64(static_cast<uint64_t>(s.size()));
            buffer_.insert(buffer_.end(), s.begin(), s.end());
        }

        std::vector<char> &get_buffer() {
            return buffer_;
        }
    };

    class BufferReader {
    public:
        const char *buffer_;
        size_t offset_;

        BufferReader(const char *buffer) : buffer_{buffer}, offset_{0} {}

        uint8_t read_u8() {
            uint8_t val = static_cast<uint8_t>(*(buffer_ + offset_));
            offset_ += 1;
            return val;
        }

        uint16_t read_u16() {
            uint16_t val;
            memcpy(&val, buffer_ + offset_, sizeof(val));
            offset_ += sizeof(val);
            uint16_t decoded = ntohs(val);
            return decoded;
        }

        uint32_t read_u32() {
            uint32_t val;
            memcpy(&val, buffer_ + offset_, sizeof(val));
            offset_ += sizeof(val);
            uint32_t decoded = ntohl(val);
            return decoded;
        }

        uint64_t read_u64() {
            uint64_t val;
            memcpy(&val, buffer_ + offset_, sizeof(val));
            offset_ += sizeof(val);
            uint64_t decoded = ntohll(val);
            return decoded;
        }

        std::string read_string() {
            uint64_t size = read_u64();
            std::string s(buffer_ + offset_, size);
            offset_ += size;
            return s;
        }
    };
} // namespace buffer