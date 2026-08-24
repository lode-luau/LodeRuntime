// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CryptoProvider.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <map>
#include <mutex>
#include <vector>

namespace lodecrypto
{
namespace
{
struct AlgHandle
{
    BCRYPT_ALG_HANDLE value = nullptr;
    ~AlgHandle() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct HashHandle
{
    BCRYPT_HASH_HANDLE value = nullptr;
    ~HashHandle() { if (value) BCryptDestroyHash(value); }
};
struct KeyHandle
{
    BCRYPT_KEY_HANDLE value = nullptr;
    ~KeyHandle() { if (value) BCryptDestroyKey(value); }
    KeyHandle() = default;
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;
};

ProviderResult<std::vector<uint8_t>> Failure(const char* message)
{
    return ProviderResult<std::vector<uint8_t>>::Failure(message);
}

const wchar_t* HashName(std::string_view name)
{
    if (name == "sha256") return BCRYPT_SHA256_ALGORITHM;
    if (name == "sha384") return BCRYPT_SHA384_ALGORITHM;
    if (name == "sha512") return BCRYPT_SHA512_ALGORITHM;
    return nullptr;
}

// Cache of HMAC-capable algorithm providers, keyed by the compile-time
// constant algorithm-name pointers. Handles are process-lifetime singletons:
// opening/closing a provider on every call made PBKDF2 with 10^6 iterations
// perform up to 10^6 BCryptOpenAlgorithmProvider/Close cycles.
BCRYPT_ALG_HANDLE CachedHmacAlgorithm(const wchar_t* name)
{
    static std::mutex mu;
    static std::map<const wchar_t*, BCRYPT_ALG_HANDLE>& cache = *new std::map<const wchar_t*, BCRYPT_ALG_HANDLE>();
    std::lock_guard<std::mutex> lock(mu);
    const auto it = cache.find(name);
    if (it != cache.end())
        return it->second;
    BCRYPT_ALG_HANDLE handle = nullptr;
    if (BCryptOpenAlgorithmProvider(&handle, name, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        return nullptr;
    cache[name] = handle;
    return handle;
}

ProviderResult<std::vector<uint8_t>> HmacInternal(std::string_view algorithm, Bytes key, Bytes data)
{
    const wchar_t* name = HashName(algorithm);
    if (!name) return Failure("unsupported hash algorithm");
    const BCRYPT_ALG_HANDLE alg = CachedHmacAlgorithm(name);
    if (!alg) return Failure("HMAC provider initialization failed");
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0)
        return Failure("HMAC provider initialization failed");
    std::vector<uint8_t> object(objectLength);
    std::vector<uint8_t> output(HashSize(algorithm));
    HashHandle hash;
    if (BCryptCreateHash(alg, &hash.value, object.data(), objectLength, const_cast<PUCHAR>(key.data), static_cast<ULONG>(key.size), 0) != 0)
        return Failure("HMAC initialization failed");
    if (data.size && BCryptHashData(hash.value, const_cast<PUCHAR>(data.data), static_cast<ULONG>(data.size), 0) != 0)
        return Failure("HMAC operation failed");
    if (BCryptFinishHash(hash.value, output.data(), static_cast<ULONG>(output.size()), 0) != 0)
        return Failure("HMAC operation failed");
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(output));
}

ProviderResult<std::vector<uint8_t>> HashInternal(std::string_view algorithm, Bytes data)
{
    const wchar_t* name = HashName(algorithm);
    if (!name) return Failure("unsupported hash algorithm");
    AlgHandle alg;
    if (BCryptOpenAlgorithmProvider(&alg.value, name, nullptr, 0) != 0)
        return Failure("hash provider initialization failed");
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(alg.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0)
        return Failure("hash provider initialization failed");
    std::vector<uint8_t> object(objectLength);
    std::vector<uint8_t> output(HashSize(algorithm));
    HashHandle hash;
    if (BCryptCreateHash(alg.value, &hash.value, object.data(), objectLength, nullptr, 0, 0) != 0)
        return Failure("hash initialization failed");
    if (data.size && BCryptHashData(hash.value, const_cast<PUCHAR>(data.data), static_cast<ULONG>(data.size), 0) != 0)
        return Failure("hash operation failed");
    if (BCryptFinishHash(hash.value, output.data(), static_cast<ULONG>(output.size()), 0) != 0)
        return Failure("hash operation failed");
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(output));
}
}

ProviderResult<std::vector<uint8_t>> RandomBytes(size_t size)
{
    if (size > kMaxRandomBytes) return Failure("random byte count exceeds limit");
    std::vector<uint8_t> output(size);
    if (size && BCryptGenRandom(nullptr, output.data(), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return Failure("system random provider failed");
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(output));
}

ProviderResult<std::vector<uint8_t>> Digest(std::string_view algorithm, Bytes data)
{
    return HashInternal(algorithm, data);
}

ProviderResult<std::vector<uint8_t>> Hmac(std::string_view algorithm, Bytes key, Bytes data)
{
    return HmacInternal(algorithm, key, data);
}

ProviderResult<std::vector<uint8_t>> Hkdf(std::string_view algorithm, Bytes ikm, size_t outputSize, Bytes salt, Bytes info)
{
    const size_t hashSize = HashSize(algorithm);
    if (!hashSize) return Failure("unsupported hash algorithm");
    if (outputSize > kMaxOutputBytes || outputSize > 255 * hashSize) return Failure("HKDF output exceeds limit");
    std::vector<uint8_t> zeroSalt(hashSize, 0);
    if (!salt.data) salt = { zeroSalt.data(), zeroSalt.size() };
    auto prk = HmacInternal(algorithm, salt, ikm);
    if (!prk.ok) return prk;
    std::vector<uint8_t> result;
    result.reserve(outputSize);
    std::vector<uint8_t> previous;
    uint8_t counter = 1;
    while (result.size() < outputSize)
    {
        std::vector<uint8_t> message;
        message.reserve(previous.size() + info.size + 1);
        message.insert(message.end(), previous.begin(), previous.end());
        if (info.size) message.insert(message.end(), info.data, info.data + info.size);
        message.push_back(counter++);
        auto block = HmacInternal(algorithm, { prk.value.data(), prk.value.size() }, { message.data(), message.size() });
        SecureClear(message);
        if (!block.ok) { SecureClear(prk.value); return block; }
        SecureClear(previous);
        previous = std::move(block.value);
        const size_t take = (std::min)(previous.size(), outputSize - result.size());
        result.insert(result.end(), previous.begin(), previous.begin() + take);
    }
    SecureClear(prk.value);
    SecureClear(previous);
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> Pbkdf2(std::string_view algorithm, Bytes password, Bytes salt, uint32_t iterations, size_t outputSize)
{
    const size_t hashSize = HashSize(algorithm);
    if (!hashSize) return Failure("unsupported hash algorithm");
    if (iterations == 0 || iterations > kMaxPbkdf2Iterations || outputSize > kMaxOutputBytes)
        return Failure("PBKDF2 parameters exceed limit");
    std::vector<uint8_t> result;
    result.reserve(outputSize);
    for (uint32_t blockIndex = 1; result.size() < outputSize; ++blockIndex)
    {
        std::vector<uint8_t> first;
        if (salt.size) first.insert(first.end(), salt.data, salt.data + salt.size);
        first.push_back(static_cast<uint8_t>(blockIndex >> 24));
        first.push_back(static_cast<uint8_t>(blockIndex >> 16));
        first.push_back(static_cast<uint8_t>(blockIndex >> 8));
        first.push_back(static_cast<uint8_t>(blockIndex));
        auto u = HmacInternal(algorithm, password, { first.data(), first.size() });
        SecureClear(first);
        if (!u.ok) return u;
        std::vector<uint8_t> accumulator = u.value;
        for (uint32_t i = 1; i < iterations; ++i)
        {
            auto next = HmacInternal(algorithm, password, { u.value.data(), u.value.size() });
            if (!next.ok) { SecureClear(u.value); SecureClear(accumulator); return next; }
            SecureClear(u.value);
            u.value = std::move(next.value);
            for (size_t j = 0; j < accumulator.size(); ++j) accumulator[j] ^= u.value[j];
        }
        const size_t take = (std::min)(accumulator.size(), outputSize - result.size());
        result.insert(result.end(), accumulator.begin(), accumulator.begin() + take);
        SecureClear(u.value);
        SecureClear(accumulator);
    }
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(result));
}

ProviderResult<std::vector<uint8_t>> AeadEncrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes plaintext, Bytes aad)
{
    if (algorithm != "aes-256-gcm") return Failure("unsupported AEAD algorithm");
    if (key.size != 32 || nonce.size != 12) return Failure("AES-256-GCM requires a 32-byte key and 12-byte nonce");
    if (plaintext.size > kMaxOutputBytes - 16) return Failure("plaintext exceeds limit");
    AlgHandle alg;
    if (BCryptOpenAlgorithmProvider(&alg.value, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return Failure("AES provider initialization failed");
    if (BCryptSetProperty(alg.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0)
        return Failure("AES-GCM initialization failed");
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(alg.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0)
        return Failure("AES provider initialization failed");
    std::vector<uint8_t> object(objectLength);
    KeyHandle handle;
    if (BCryptGenerateSymmetricKey(alg.value, &handle.value, object.data(), objectLength, const_cast<PUCHAR>(key.data), static_cast<ULONG>(key.size), 0) != 0)
        return Failure("AES key initialization failed");
    std::vector<uint8_t> output(plaintext.size + 16);
    std::array<uint8_t, 16> tag{};
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data); info.cbNonce = static_cast<ULONG>(nonce.size);
    info.pbAuthData = const_cast<PUCHAR>(aad.data); info.cbAuthData = static_cast<ULONG>(aad.size);
    info.pbTag = tag.data(); info.cbTag = static_cast<ULONG>(tag.size());
    ULONG written = 0;
    if (BCryptEncrypt(handle.value, const_cast<PUCHAR>(plaintext.data), static_cast<ULONG>(plaintext.size), &info, nullptr, 0, output.data(), static_cast<ULONG>(plaintext.size), &written, 0) != 0)
        return Failure("AES-GCM encryption failed");
    std::copy(tag.begin(), tag.end(), output.begin() + written);
    output.resize(written + tag.size());
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(output));
}

ProviderResult<std::vector<uint8_t>> AeadDecrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes ciphertextAndTag, Bytes aad)
{
    if (algorithm != "aes-256-gcm") return Failure("unsupported AEAD algorithm");
    if (key.size != 32 || nonce.size != 12) return Failure("AES-256-GCM requires a 32-byte key and 12-byte nonce");
    if (ciphertextAndTag.size < 16) return ProviderResult<std::vector<uint8_t>>::Failure("authentication failed", true);
    const size_t cipherSize = ciphertextAndTag.size - 16;
    if (cipherSize > kMaxOutputBytes) return ProviderResult<std::vector<uint8_t>>::Failure("ciphertext exceeds output limit", false);
    AlgHandle alg;
    if (BCryptOpenAlgorithmProvider(&alg.value, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return Failure("AES provider initialization failed");
    if (BCryptSetProperty(alg.value, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0)
        return Failure("AES-GCM initialization failed");
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(alg.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0)
        return Failure("AES provider initialization failed");
    std::vector<uint8_t> object(objectLength);
    KeyHandle handle;
    if (BCryptGenerateSymmetricKey(alg.value, &handle.value, object.data(), objectLength, const_cast<PUCHAR>(key.data), static_cast<ULONG>(key.size), 0) != 0)
        return Failure("AES key initialization failed");
    std::vector<uint8_t> output(cipherSize);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data); info.cbNonce = static_cast<ULONG>(nonce.size);
    info.pbAuthData = const_cast<PUCHAR>(aad.data); info.cbAuthData = static_cast<ULONG>(aad.size);
    info.pbTag = const_cast<PUCHAR>(ciphertextAndTag.data + cipherSize); info.cbTag = 16;
    ULONG written = 0;
    if (BCryptDecrypt(handle.value, const_cast<PUCHAR>(ciphertextAndTag.data), static_cast<ULONG>(cipherSize), &info, nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0) != 0)
        return ProviderResult<std::vector<uint8_t>>::Failure("authentication failed", true);
    output.resize(written);
    return ProviderResult<std::vector<uint8_t>>::Success(std::move(output));
}

}
#else
#include "CryptoProvider.hpp"
namespace lodecrypto { }
#endif
