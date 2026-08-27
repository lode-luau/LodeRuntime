// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

#ifndef _WIN32

#include "CryptoProvider.hpp"

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace lodecrypto
{
namespace
{
using BytesVector = std::vector<uint8_t>;

template <typename T, void (*Deleter)(T*)>
using OpenSslPtr = std::unique_ptr<T, decltype(Deleter)>;

using EvpMdContextPtr = OpenSslPtr<EVP_MD_CTX, EVP_MD_CTX_free>;
using EvpCipherCtxPtr = OpenSslPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using EvpPkeyPtr = OpenSslPtr<EVP_PKEY, EVP_PKEY_free>;
using EvpPkeyCtxPtr = OpenSslPtr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
using Pkcs8Ptr = OpenSslPtr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free>;

ProviderResult<BytesVector> Failure(const char* message)
{
    return ProviderResult<BytesVector>::Failure(message);
}

const EVP_MD* HashFunction(std::string_view algorithm)
{
    if (algorithm == "sha256") return EVP_sha256();
    if (algorithm == "sha384") return EVP_sha384();
    if (algorithm == "sha512") return EVP_sha512();
    return nullptr;
}

const EVP_CIPHER* AeadCipher(std::string_view algorithm)
{
    return algorithm == "aes-256-gcm" ? EVP_aes_256_gcm() : nullptr;
}

const EVP_MD* SignatureHash(std::string_view algorithm)
{
    if (algorithm == "rsa-pss-sha256" || algorithm == "rsa-oaep-sha256" ||
        algorithm == "ecdsa-p256-sha256") return EVP_sha256();
    if (algorithm == "rsa-pss-sha384" || algorithm == "rsa-oaep-sha384") return EVP_sha384();
    if (algorithm == "rsa-pss-sha512" || algorithm == "rsa-oaep-sha512") return EVP_sha512();
    return nullptr;
}

bool FitsInt(size_t value)
{
    return value <= static_cast<size_t>(INT_MAX);
}

EvpPkeyPtr ReadPrivateKey(Bytes der)
{
    if (!der.data || der.size == 0 || !FitsInt(der.size)) return {};
    const unsigned char* cursor = der.data;
    EvpPkeyPtr key(d2i_AutoPrivateKey(nullptr, &cursor, static_cast<long>(der.size)));
    return key && cursor == der.data + der.size ? std::move(key) : EvpPkeyPtr();
}

EvpPkeyPtr ReadPublicKey(Bytes der)
{
    if (!der.data || der.size == 0 || !FitsInt(der.size)) return {};
    const unsigned char* cursor = der.data;
    EvpPkeyPtr key(d2i_PUBKEY(nullptr, &cursor, static_cast<long>(der.size)));
    return key && cursor == der.data + der.size ? std::move(key) : EvpPkeyPtr();
}

ProviderResult<BytesVector> EncodePrivateKey(EVP_PKEY* key)
{
    Pkcs8Ptr info(EVP_PKEY2PKCS8(key));
    if (!info) return Failure("private key encoding failed");
    const int size = i2d_PKCS8_PRIV_KEY_INFO(info.get(), nullptr);
    if (size <= 0 || static_cast<size_t>(size) > kMaxOutputBytes) return Failure("private key encoding failed");
    BytesVector output(static_cast<size_t>(size));
    unsigned char* cursor = output.data();
    if (i2d_PKCS8_PRIV_KEY_INFO(info.get(), &cursor) != size) return Failure("private key encoding failed");
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> EncodePublicKey(EVP_PKEY* key)
{
    const int size = i2d_PUBKEY(key, nullptr);
    if (size <= 0 || static_cast<size_t>(size) > kMaxOutputBytes) return Failure("public key encoding failed");
    BytesVector output(static_cast<size_t>(size));
    unsigned char* cursor = output.data();
    if (i2d_PUBKEY(key, &cursor) != size) return Failure("public key encoding failed");
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> HmacInternal(std::string_view algorithm, Bytes key, Bytes data)
{
    const EVP_MD* md = HashFunction(algorithm);
    if (!md) return Failure("unsupported hash algorithm");
    if (!FitsInt(key.size) || !FitsInt(data.size)) return Failure("HMAC input exceeds limit");
    BytesVector output(static_cast<size_t>(EVP_MD_size(md)));
    unsigned int written = 0;
    if (!HMAC(md, key.data, static_cast<int>(key.size), data.data,
              data.size, output.data(), &written)) return Failure("HMAC operation failed");
    output.resize(written);
    return ProviderResult<BytesVector>::Success(std::move(output));
}
}

ProviderResult<BytesVector> RandomBytes(size_t size)
{
    if (size > kMaxRandomBytes) return Failure("random byte count exceeds limit");
    BytesVector output(size);
    if (size && RAND_bytes(output.data(), static_cast<int>(size)) != 1)
        return Failure("system random provider failed");
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Digest(std::string_view algorithm, Bytes data)
{
    const EVP_MD* md = HashFunction(algorithm);
    if (!md) return Failure("unsupported hash algorithm");
    EvpMdContextPtr context(EVP_MD_CTX_new());
    EVP_MD_CTX* raw = context.get();
    if (!raw) return Failure("hash initialization failed");
    unsigned int written = 0;
    BytesVector output(static_cast<size_t>(EVP_MD_size(md)));
    const bool ok = EVP_DigestInit_ex(raw, md, nullptr) == 1 &&
        EVP_DigestUpdate(raw, data.data, data.size) == 1 &&
        EVP_DigestFinal_ex(raw, output.data(), &written) == 1;
    if (!ok) return Failure("hash operation failed");
    output.resize(written);
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Hmac(std::string_view algorithm, Bytes key, Bytes data)
{
    return HmacInternal(algorithm, key, data);
}

ProviderResult<BytesVector> Hkdf(std::string_view algorithm, Bytes ikm, size_t outputSize, Bytes salt, Bytes info)
{
    const EVP_MD* md = HashFunction(algorithm);
    const size_t hashSize = HashSize(algorithm);
    if (!md || !hashSize) return Failure("unsupported hash algorithm");
    if (outputSize > kMaxOutputBytes || outputSize > 255 * hashSize)
        return Failure("HKDF output exceeds limit");
    if (!FitsInt(ikm.size) || !FitsInt(salt.size) || !FitsInt(info.size))
        return Failure("HKDF input exceeds limit");

    BytesVector zeroSalt(hashSize, 0);
    if (!salt.data) salt = { zeroSalt.data(), zeroSalt.size() };
    EvpPkeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
    if (!context || EVP_PKEY_derive_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(context.get(), md) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(context.get(), salt.data, static_cast<int>(salt.size)) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(context.get(), ikm.data, static_cast<int>(ikm.size)) <= 0 ||
        (info.size && EVP_PKEY_CTX_add1_hkdf_info(context.get(), info.data, static_cast<int>(info.size)) <= 0))
        return Failure("HKDF initialization failed");
    BytesVector output(outputSize);
    size_t written = outputSize;
    if (EVP_PKEY_derive(context.get(), output.data(), &written) <= 0)
        return Failure("HKDF operation failed");
    output.resize(written);
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Pbkdf2(std::string_view algorithm, Bytes password, Bytes salt, uint32_t iterations, size_t outputSize)
{
    const EVP_MD* md = HashFunction(algorithm);
    if (!md) return Failure("unsupported hash algorithm");
    if (iterations == 0 || iterations > kMaxPbkdf2Iterations || outputSize > kMaxOutputBytes ||
        !FitsInt(password.size) || !FitsInt(salt.size) || !FitsInt(outputSize))
        return Failure("PBKDF2 parameters exceed limit");
    BytesVector output(outputSize);
    if (outputSize && PKCS5_PBKDF2_HMAC(
            reinterpret_cast<const char*>(password.data), static_cast<int>(password.size),
            salt.data, static_cast<int>(salt.size), static_cast<int>(iterations), md,
            static_cast<int>(outputSize), output.data()) != 1)
        return Failure("PBKDF2 operation failed");
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> AeadEncrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes plaintext, Bytes aad)
{
    const EVP_CIPHER* cipher = AeadCipher(algorithm);
    if (!cipher) return Failure("unsupported AEAD algorithm");
    if (key.size != 32 || nonce.size != 12) return Failure("AES-256-GCM requires a 32-byte key and 12-byte nonce");
    if (plaintext.size > kMaxOutputBytes - 16 || !FitsInt(plaintext.size) || !FitsInt(aad.size))
        return Failure("plaintext exceeds limit");
    EvpCipherCtxPtr context(EVP_CIPHER_CTX_new());
    if (!context || EVP_EncryptInit_ex(context.get(), cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size), nullptr) != 1 ||
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr, key.data, nonce.data) != 1)
        return Failure("AES-GCM initialization failed");
    int written = 0;
    if (aad.size && EVP_EncryptUpdate(context.get(), nullptr, &written, aad.data, static_cast<int>(aad.size)) != 1)
        return Failure("AES-GCM encryption failed");
    BytesVector output(plaintext.size + 16);
    int total = 0;
    if (plaintext.size && EVP_EncryptUpdate(context.get(), output.data(), &written,
            plaintext.data, static_cast<int>(plaintext.size)) != 1) return Failure("AES-GCM encryption failed");
    total += written;
    if (EVP_EncryptFinal_ex(context.get(), output.data() + total, &written) != 1)
        return Failure("AES-GCM encryption failed");
    total += written;
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, 16, output.data() + total) != 1)
        return Failure("AES-GCM encryption failed");
    output.resize(static_cast<size_t>(total) + 16);
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> AeadDecrypt(std::string_view algorithm, Bytes key, Bytes nonce, Bytes ciphertextAndTag, Bytes aad)
{
    const EVP_CIPHER* cipher = AeadCipher(algorithm);
    if (!cipher) return Failure("unsupported AEAD algorithm");
    if (key.size != 32 || nonce.size != 12) return Failure("AES-256-GCM requires a 32-byte key and 12-byte nonce");
    if (ciphertextAndTag.size < 16)
        return ProviderResult<BytesVector>::Failure("authentication failed", true);
    const size_t ciphertextSize = ciphertextAndTag.size - 16;
    if (ciphertextSize > kMaxOutputBytes || !FitsInt(ciphertextSize) || !FitsInt(aad.size))
        return Failure("ciphertext exceeds output limit");
    EvpCipherCtxPtr context(EVP_CIPHER_CTX_new());
    if (!context || EVP_DecryptInit_ex(context.get(), cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size), nullptr) != 1 ||
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr, key.data, nonce.data) != 1)
        return Failure("AES-GCM initialization failed");
    int written = 0;
    if (aad.size && EVP_DecryptUpdate(context.get(), nullptr, &written, aad.data, static_cast<int>(aad.size)) != 1)
        return ProviderResult<BytesVector>::Failure("authentication failed", true);
    // Keep room for the final block even when the ciphertext is empty. This
    // avoids forming an invalid pointer from output.data() + total below.
    BytesVector output(ciphertextSize + 16);
    int total = 0;
    if (ciphertextSize && EVP_DecryptUpdate(context.get(), output.data(), &written,
            ciphertextAndTag.data, static_cast<int>(ciphertextSize)) != 1)
        return ProviderResult<BytesVector>::Failure("authentication failed", true);
    total += written;
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, 16,
            const_cast<uint8_t*>(ciphertextAndTag.data + ciphertextSize)) != 1)
        return ProviderResult<BytesVector>::Failure("authentication failed", true);
    if (EVP_DecryptFinal_ex(context.get(), output.data() + total, &written) != 1)
        return ProviderResult<BytesVector>::Failure("authentication failed", true);
    output.resize(static_cast<size_t>(total + written));
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<KeyPairBytes> GenerateKeyPair(std::string_view algorithm)
{
    int type = 0;
    if (algorithm == "rsa-3072") type = EVP_PKEY_RSA;
    else if (algorithm == "p256") type = EVP_PKEY_EC;
    else return ProviderResult<KeyPairBytes>::Failure("unsupported key pair algorithm");

    EvpPkeyCtxPtr context(EVP_PKEY_CTX_new_id(type, nullptr));
    if (!context || EVP_PKEY_keygen_init(context.get()) <= 0) return ProviderResult<KeyPairBytes>::Failure("key generation failed");
    if (type == EVP_PKEY_RSA) {
        if (EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 3072) <= 0)
            return ProviderResult<KeyPairBytes>::Failure("RSA key generation failed");
    } else if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) <= 0) {
        return ProviderResult<KeyPairBytes>::Failure("P-256 key generation failed");
    }
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(context.get(), &raw) <= 0) return ProviderResult<KeyPairBytes>::Failure("key generation failed");
    EvpPkeyPtr key(raw);
    auto privateKey = EncodePrivateKey(key.get());
    auto publicKey = EncodePublicKey(key.get());
    if (!privateKey.ok || !publicKey.ok)
        return ProviderResult<KeyPairBytes>::Failure("key export failed");
    return ProviderResult<KeyPairBytes>::Success({std::string(algorithm), std::move(privateKey.value), std::move(publicKey.value)});
}

ProviderResult<BytesVector> Sign(std::string_view algorithm, Bytes privateKey, Bytes data)
{
    const EVP_MD* md = SignatureHash(algorithm);
    const bool rsa = algorithm.rfind("rsa-pss-sha", 0) == 0;
    const bool ecdsa = algorithm == "ecdsa-p256-sha256";
    if (!md || (!rsa && !ecdsa)) return Failure("unsupported signature algorithm");
    EvpPkeyPtr key = ReadPrivateKey(privateKey);
    if (!key) return Failure("invalid private key");
    EvpMdContextPtr context(EVP_MD_CTX_new());
    EVP_MD_CTX* raw = context.get();
    if (!raw || EVP_DigestSignInit(raw, nullptr, md, nullptr, key.get()) != 1)
        return Failure("signature initialization failed");
    if (rsa) {
        EVP_PKEY_CTX* pkeyContext = EVP_MD_CTX_pkey_ctx(raw);
        if (!pkeyContext || EVP_PKEY_CTX_set_rsa_padding(pkeyContext, RSA_PKCS1_PSS_PADDING) <= 0 ||
            EVP_PKEY_CTX_set_rsa_pss_saltlen(pkeyContext, EVP_MD_size(md)) <= 0)
            return Failure("RSA-PSS initialization failed");
    }
    if (EVP_DigestSignUpdate(raw, data.data, data.size) != 1) return Failure("signature operation failed");
    size_t size = 0;
    if (EVP_DigestSignFinal(raw, nullptr, &size) != 1 || size > kMaxOutputBytes) return Failure("signature operation failed");
    BytesVector signature(size);
    if (EVP_DigestSignFinal(raw, signature.data(), &size) != 1) return Failure("signature operation failed");
    signature.resize(size);
    return ProviderResult<BytesVector>::Success(std::move(signature));
}

ProviderResult<bool> Verify(std::string_view algorithm, Bytes publicKey, Bytes data, Bytes signature)
{
    const EVP_MD* md = SignatureHash(algorithm);
    const bool rsa = algorithm.rfind("rsa-pss-sha", 0) == 0;
    const bool ecdsa = algorithm == "ecdsa-p256-sha256";
    if (!md || (!rsa && !ecdsa)) return ProviderResult<bool>::Failure("unsupported signature algorithm");
    EvpPkeyPtr key = ReadPublicKey(publicKey);
    if (!key) return ProviderResult<bool>::Failure("invalid public key");
    EvpMdContextPtr context(EVP_MD_CTX_new());
    EVP_MD_CTX* raw = context.get();
    if (!raw || EVP_DigestVerifyInit(raw, nullptr, md, nullptr, key.get()) != 1)
        return ProviderResult<bool>::Failure("signature initialization failed");
    if (rsa) {
        EVP_PKEY_CTX* pkeyContext = EVP_MD_CTX_pkey_ctx(raw);
        if (!pkeyContext || EVP_PKEY_CTX_set_rsa_padding(pkeyContext, RSA_PKCS1_PSS_PADDING) <= 0 ||
            EVP_PKEY_CTX_set_rsa_pss_saltlen(pkeyContext, EVP_MD_size(md)) <= 0)
            return ProviderResult<bool>::Failure("RSA-PSS initialization failed");
    }
    if (EVP_DigestVerifyUpdate(raw, data.data, data.size) != 1)
        return ProviderResult<bool>::Failure("signature operation failed");
    const int result = EVP_DigestVerifyFinal(raw, signature.data, signature.size);
    if (result == 1) return ProviderResult<bool>::Success(true);
    if (result == 0) return ProviderResult<bool>::Success(false);
    return ProviderResult<bool>::Failure("signature verification failed");
}

ProviderResult<BytesVector> Encrypt(std::string_view algorithm, Bytes publicKey, Bytes plaintext)
{
    const EVP_MD* md = SignatureHash(algorithm);
    if (!md || algorithm.rfind("rsa-oaep-sha", 0) != 0) return Failure("unsupported encryption algorithm");
    EvpPkeyPtr key = ReadPublicKey(publicKey);
    if (!key || !FitsInt(plaintext.size)) return Failure("invalid RSA public key");
    EvpPkeyCtxPtr context(EVP_PKEY_CTX_new(key.get(), nullptr));
    if (!context || EVP_PKEY_encrypt_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_OAEP_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_oaep_md(context.get(), md) <= 0 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context.get(), md) <= 0)
        return Failure("RSA-OAEP initialization failed");
    size_t size = 0;
    if (EVP_PKEY_encrypt(context.get(), nullptr, &size, plaintext.data, plaintext.size) <= 0 || size > kMaxOutputBytes)
        return Failure("RSA-OAEP encryption failed");
    BytesVector output(size);
    if (EVP_PKEY_encrypt(context.get(), output.data(), &size, plaintext.data, plaintext.size) <= 0)
        return Failure("RSA-OAEP encryption failed");
    output.resize(size);
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Decrypt(std::string_view algorithm, Bytes privateKey, Bytes ciphertext)
{
    const EVP_MD* md = SignatureHash(algorithm);
    if (!md || algorithm.rfind("rsa-oaep-sha", 0) != 0) return Failure("unsupported encryption algorithm");
    EvpPkeyPtr key = ReadPrivateKey(privateKey);
    if (!key) return Failure("invalid RSA private key");
    EvpPkeyCtxPtr context(EVP_PKEY_CTX_new(key.get(), nullptr));
    if (!context || EVP_PKEY_decrypt_init(context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_OAEP_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_oaep_md(context.get(), md) <= 0 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(context.get(), md) <= 0)
        return Failure("RSA-OAEP initialization failed");
    size_t size = 0;
    if (EVP_PKEY_decrypt(context.get(), nullptr, &size, ciphertext.data, ciphertext.size) <= 0 || size > kMaxOutputBytes)
        return Failure("RSA-OAEP decryption failed");
    BytesVector output(size);
    if (EVP_PKEY_decrypt(context.get(), output.data(), &size, ciphertext.data, ciphertext.size) <= 0)
        return Failure("RSA-OAEP decryption failed");
    output.resize(size);
    return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Derive(std::string_view algorithm, Bytes privateKey, Bytes peerPublicKey)
{
    if (algorithm != "ecdh-p256") return Failure("unsupported key agreement algorithm");
    EvpPkeyPtr privateKeyObject = ReadPrivateKey(privateKey);
    EvpPkeyPtr publicKeyObject = ReadPublicKey(peerPublicKey);
    if (!privateKeyObject || !publicKeyObject) return Failure("invalid P-256 key");
    EvpPkeyCtxPtr context(EVP_PKEY_CTX_new(privateKeyObject.get(), nullptr));
    if (!context || EVP_PKEY_derive_init(context.get()) <= 0 ||
        EVP_PKEY_derive_set_peer(context.get(), publicKeyObject.get()) <= 0)
        return Failure("ECDH agreement failed");
    size_t size = 0;
    if (EVP_PKEY_derive(context.get(), nullptr, &size) <= 0 || size == 0 || size > kMaxOutputBytes)
        return Failure("ECDH derivation failed");
    BytesVector output(size);
    if (EVP_PKEY_derive(context.get(), output.data(), &size) <= 0)
        return Failure("ECDH derivation failed");
    output.resize(size);
    return ProviderResult<BytesVector>::Success(std::move(output));
}
}

#endif
