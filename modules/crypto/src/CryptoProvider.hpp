// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace lodecrypto
{

constexpr size_t kMaxInputBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxOutputBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxRandomBytes = 1024u * 1024u;
constexpr uint32_t kMaxPbkdf2Iterations = 1000000u;

struct Bytes
{
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct KeyPairBytes
{
    std::string algorithm;
    std::vector<uint8_t> privateKey;
    std::vector<uint8_t> publicKey;
};

struct ProviderError
{
    std::string message;
    bool authenticationFailure = false;
};

template <typename T>
struct ProviderResult
{
    T value{};
    ProviderError error{};
    bool ok = false;

    static ProviderResult Success(T result)
    {
        return { std::move(result), {}, true };
    }

    static ProviderResult Failure(std::string message, bool authentication = false)
    {
        return { {}, { std::move(message), authentication }, false };
    }
};

void SecureClear(std::vector<uint8_t>& bytes);
bool ConstantTimeEqual(Bytes left, Bytes right);
ProviderResult<std::vector<uint8_t>> HexEncode(Bytes data);
ProviderResult<std::vector<uint8_t>> HexDecode(Bytes text);
ProviderResult<std::vector<uint8_t>> Base64Encode(Bytes data);
ProviderResult<std::vector<uint8_t>> Base64Decode(Bytes text);
ProviderResult<std::vector<uint8_t>> PemEncode(std::string_view label, Bytes der);
ProviderResult<std::vector<uint8_t>> PemDecode(Bytes pem, std::string_view expectedLabel);

bool IsHashAlgorithm(std::string_view algorithm);
size_t HashSize(std::string_view algorithm);

ProviderResult<std::vector<uint8_t>> RandomBytes(size_t size);
ProviderResult<std::vector<uint8_t>> Digest(std::string_view algorithm, Bytes data);
ProviderResult<std::vector<uint8_t>> Hmac(std::string_view algorithm, Bytes key, Bytes data);
ProviderResult<std::vector<uint8_t>> Hkdf(std::string_view algorithm, Bytes ikm, size_t outputSize, Bytes salt, Bytes info);
ProviderResult<std::vector<uint8_t>> Pbkdf2(std::string_view algorithm, Bytes password, Bytes salt, uint32_t iterations, size_t outputSize);
ProviderResult<std::vector<uint8_t>> AeadEncrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes plaintext, Bytes aad);
ProviderResult<std::vector<uint8_t>> AeadDecrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes ciphertextAndTag, Bytes aad);
ProviderResult<KeyPairBytes> GenerateKeyPair(std::string_view algorithm);
ProviderResult<std::vector<uint8_t>> Sign(std::string_view algorithm, Bytes privateKey, Bytes data);
ProviderResult<bool> Verify(std::string_view algorithm, Bytes publicKey, Bytes data, Bytes signature);
ProviderResult<std::vector<uint8_t>> Encrypt(std::string_view algorithm, Bytes publicKey, Bytes plaintext);
ProviderResult<std::vector<uint8_t>> Decrypt(std::string_view algorithm, Bytes privateKey, Bytes ciphertext);
ProviderResult<std::vector<uint8_t>> Derive(std::string_view algorithm, Bytes privateKey, Bytes peerPublicKey);

} // namespace lodecrypto
