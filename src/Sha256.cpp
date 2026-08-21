// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Lode::Detail
{

namespace
{

void Sha256Transform(std::uint32_t state[8], const std::uint8_t block[64])
{
    static const std::uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u,
        0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u,
        0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu,
        0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };

    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
               (std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i)
    {
        const std::uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + K[i] + w[i];
        const std::uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

class Sha256Hasher
{
public:
    void Update(const char* input, size_t size)
    {
        totalBytes_ += size;
        while (size != 0)
        {
            const size_t copied = std::min(size, block_.size() - blockSize_);
            std::memcpy(block_.data() + blockSize_, input, copied);
            blockSize_ += copied;
            input += copied;
            size -= copied;
            if (blockSize_ == block_.size())
            {
                Sha256Transform(state_.data(), block_.data());
                blockSize_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> Finalize()
    {
        const std::uint64_t bitLength = totalBytes_ * 8;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56)
        {
            std::fill(block_.begin() + blockSize_, block_.end(), 0);
            Sha256Transform(state_.data(), block_.data());
            blockSize_ = 0;
        }
        std::fill(block_.begin() + blockSize_, block_.begin() + 56, 0);
        for (int i = 7; i >= 0; --i)
            block_[56 + (7 - i)] = std::uint8_t((bitLength >> (i * 8)) & 0xff);
        Sha256Transform(state_.data(), block_.data());

        std::array<std::uint8_t, 32> output{};
        for (int i = 0; i < 8; ++i)
        {
            output[i * 4] = std::uint8_t(state_[i] >> 24);
            output[i * 4 + 1] = std::uint8_t(state_[i] >> 16);
            output[i * 4 + 2] = std::uint8_t(state_[i] >> 8);
            output[i * 4 + 3] = std::uint8_t(state_[i]);
        }
        return output;
    }

private:
    std::array<std::uint32_t, 8> state_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    std::array<std::uint8_t, 64> block_{};
    size_t blockSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

} // namespace

std::array<std::uint8_t, 32> Sha256(std::string_view data)
{
    Sha256Hasher hasher;
    hasher.Update(data.data(), data.size());
    return hasher.Finalize();
}

std::string ToHex(const std::array<std::uint8_t, 32>& bytes)
{
    static const char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(64);
    for (const std::uint8_t byte : bytes)
    {
        output.push_back(hex[byte >> 4]);
        output.push_back(hex[byte & 0x0f]);
    }
    return output;
}

std::string Sha256Hex(std::string_view data)
{
    return ToHex(Sha256(data));
}

std::string Sha256FileHex(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Cannot open file for SHA-256: " + path.string());

    Sha256Hasher hasher;
    std::array<char, 64 * 1024> buffer{};
    while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() != 0)
        hasher.Update(buffer.data(), static_cast<size_t>(file.gcount()));
    if (!file.eof())
        throw std::runtime_error("Cannot read file for SHA-256: " + path.string());

    return ToHex(hasher.Finalize());
}

} // namespace Lode::Detail
