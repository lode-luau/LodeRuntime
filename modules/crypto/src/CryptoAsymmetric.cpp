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
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lodecrypto
{
namespace
{
using BytesVector = std::vector<uint8_t>;

struct AlgHandle
{
    BCRYPT_ALG_HANDLE value = nullptr;
    ~AlgHandle() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};
struct KeyHandle
{
    BCRYPT_KEY_HANDLE value = nullptr;
    ~KeyHandle() { if (value) BCryptDestroyKey(value); }
    KeyHandle() = default;
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;
    KeyHandle(KeyHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    KeyHandle& operator=(KeyHandle&& other) noexcept { if (this != &other) { if (value) BCryptDestroyKey(value); value = other.value; other.value = nullptr; } return *this; }
};
struct SecretHandle
{
    BCRYPT_SECRET_HANDLE value = nullptr;
    ~SecretHandle() { if (value) BCryptDestroySecret(value); }
};

ProviderResult<BytesVector> Failure(const char* message)
{
    return ProviderResult<BytesVector>::Failure(message);
}
ProviderResult<BytesVector> Failure(const std::string& message)
{
    return ProviderResult<BytesVector>::Failure(message);
}

void AppendLength(BytesVector& out, size_t length)
{
    if (length < 128) { out.push_back(static_cast<uint8_t>(length)); return; }
    uint8_t bytes[sizeof(size_t)]; size_t count = 0;
    while (length) { bytes[count++] = static_cast<uint8_t>(length & 0xff); length >>= 8; }
    out.push_back(static_cast<uint8_t>(0x80 | count));
    while (count) out.push_back(bytes[--count]);
}

BytesVector Tlv(uint8_t tag, const BytesVector& content)
{
    BytesVector out; out.reserve(1 + 9 + content.size()); out.push_back(tag); AppendLength(out, content.size());
    out.insert(out.end(), content.begin(), content.end()); return out;
}
BytesVector Sequence(std::initializer_list<BytesVector> parts)
{
    BytesVector content; for (const auto& part : parts) content.insert(content.end(), part.begin(), part.end());
    return Tlv(0x30, content);
}
BytesVector Integer(const BytesVector& value)
{
    size_t first = 0; while (first + 1 < value.size() && value[first] == 0) ++first;
    BytesVector content(value.begin() + first, value.end()); if (content.empty()) content.push_back(0);
    if (content[0] & 0x80) content.insert(content.begin(), 0);
    return Tlv(0x02, content);
}
BytesVector IntegerU32(uint32_t value)
{
    BytesVector bytes{ static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
    while (bytes.size() > 1 && bytes[0] == 0) bytes.erase(bytes.begin()); return Integer(bytes);
}
BytesVector OctetString(const BytesVector& value) { return Tlv(0x04, value); }
BytesVector BitString(const BytesVector& value)
{
    BytesVector content; content.push_back(0); content.insert(content.end(), value.begin(), value.end()); return Tlv(0x03, content);
}
BytesVector Oid(const std::initializer_list<uint8_t>& value) { return Tlv(0x06, BytesVector(value)); }
BytesVector Null() { return { 0x05, 0x00 }; }

class DerReader
{
public:
    explicit DerReader(Bytes bytes) : data_(bytes.data), size_(bytes.size) {}
    bool Empty() const { return position_ == size_; }
    bool Read(uint8_t tag, Bytes& content)
    {
        if (position_ >= size_ || data_[position_] != tag) return false;
        ++position_;
        size_t length = 0; if (!ReadLength(length) || length > size_ - position_) return false;
        content = { data_ + position_, length }; position_ += length; return true;
    }
    bool ReadVector(uint8_t tag, BytesVector& content)
    {
        Bytes bytes; if (!Read(tag, bytes)) return false; content.assign(bytes.data, bytes.data + bytes.size); return true;
    }
    bool ReadInteger(BytesVector& value)
    {
        Bytes bytes; if (!Read(0x02, bytes) || bytes.size == 0) return false;
        if ((bytes.data[0] & 0x80) != 0) return false;
        if (bytes.size > 1 && bytes.data[0] == 0 && (bytes.data[1] & 0x80) == 0) return false;
        size_t first = 0; while (first + 1 < bytes.size && bytes.data[first] == 0) ++first;
        value.assign(bytes.data + first, bytes.data + bytes.size); return true;
    }
    bool ReadBitString(BytesVector& value)
    {
        Bytes bytes; if (!Read(0x03, bytes) || bytes.size == 0 || bytes.data[0] != 0) return false;
        value.assign(bytes.data + 1, bytes.data + bytes.size); return true;
    }
private:
    bool ReadLength(size_t& length)
    {
        if (position_ >= size_) return false; const uint8_t first = data_[position_++];
        if ((first & 0x80) == 0) { length = first; return true; }
        const size_t count = first & 0x7f; if (count == 0 || count > sizeof(size_t) || count > size_ - position_) return false;
        if (data_[position_] == 0) return false; length = 0;
        for (size_t i = 0; i < count; ++i) { if (length > (std::numeric_limits<size_t>::max() >> 8)) return false; length = (length << 8) | data_[position_++]; }
        return length >= 128;
    }
    const uint8_t* data_ = nullptr; size_t size_ = 0; size_t position_ = 0;
};

bool EqualBytes(Bytes left, const std::initializer_list<uint8_t>& right)
{
    return left.size == right.size() && std::equal(left.data, left.data + left.size, right.begin());
}

bool ReadAlgorithmIdentifier(DerReader& reader, bool& rsa)
{
    Bytes sequence; if (!reader.Read(0x30, sequence)) return false; DerReader algorithm(sequence); Bytes oid;
    if (!algorithm.Read(0x06, oid)) return false;
    rsa = EqualBytes(oid, { 0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01 });
    const bool ec = EqualBytes(oid, { 0x2a,0x86,0x48,0xce,0x3d,0x02,0x01 });
    if (!rsa && !ec) return false;
    if (rsa) { Bytes ignored; if (!algorithm.Read(0x05, ignored) || ignored.size != 0) return false; }
    else { Bytes curve; if (!algorithm.Read(0x06, curve) || !EqualBytes(curve, { 0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07 })) return false; }
    return algorithm.Empty();
}

struct RsaParts { BytesVector n, e, d, p, q, dp, dq, qi; };
struct EccParts { BytesVector x, y, d; };

bool ParseRsaPrivate(Bytes input, RsaParts& out)
{
    DerReader outer(input); Bytes sequence; if (!outer.Read(0x30, sequence) || !outer.Empty()) return false; DerReader reader(sequence); BytesVector version;
    if (!reader.ReadInteger(version) || version.size() != 1 || version[0] != 0) return false;
    return reader.ReadInteger(out.n) && reader.ReadInteger(out.e) && reader.ReadInteger(out.d) && reader.ReadInteger(out.p) && reader.ReadInteger(out.q) && reader.ReadInteger(out.dp) && reader.ReadInteger(out.dq) && reader.ReadInteger(out.qi) && reader.Empty();
}

bool ParseRsaPrivateKeyInfo(Bytes input, RsaParts& out)
{
    Bytes sequence; DerReader outer(input); if (!outer.Read(0x30, sequence) || !outer.Empty()) return false; DerReader reader(sequence); BytesVector version, privateBytes; bool rsa = false;
    if (!reader.ReadInteger(version) || version.size() != 1 || version[0] != 0 || !ReadAlgorithmIdentifier(reader, rsa) || !rsa || !reader.ReadVector(0x04, privateBytes) || !reader.Empty()) return false;
    return ParseRsaPrivate({ privateBytes.data(), privateBytes.size() }, out);
}

bool ParseRsaPublicKey(Bytes input, RsaParts& out)
{
    DerReader reader(input); Bytes sequence; if (!reader.Read(0x30, sequence) || !reader.Empty()) return false; DerReader inner(sequence);
    return inner.ReadInteger(out.n) && inner.ReadInteger(out.e) && inner.Empty();
}

bool ParseRsaPublicKeyInfo(Bytes input, RsaParts& out)
{
    Bytes sequence; DerReader outer(input); if (!outer.Read(0x30, sequence) || !outer.Empty()) return false; DerReader reader(sequence); bool rsa = false; BytesVector publicBytes;
    if (!ReadAlgorithmIdentifier(reader, rsa) || !rsa || !reader.ReadBitString(publicBytes) || !reader.Empty()) return false;
    return ParseRsaPublicKey({ publicBytes.data(), publicBytes.size() }, out);
}

bool ParseEccPublicKey(Bytes input, EccParts& out)
{
    Bytes sequence; DerReader outer(input); if (!outer.Read(0x30, sequence) || !outer.Empty()) return false; DerReader reader(sequence); bool rsa = false; BytesVector publicBytes;
    if (!ReadAlgorithmIdentifier(reader, rsa) || rsa || !reader.ReadBitString(publicBytes) || publicBytes.size() != 65 || publicBytes[0] != 4 || !reader.Empty()) return false;
    out.x.assign(publicBytes.begin() + 1, publicBytes.begin() + 33); out.y.assign(publicBytes.begin() + 33, publicBytes.begin() + 65); return true;
}

bool ParseEccPrivateKeyInfo(Bytes input, EccParts& out)
{
    out.x.clear(); out.y.clear(); out.d.clear();
    Bytes sequence; DerReader outer(input); if (!outer.Read(0x30, sequence) || !outer.Empty()) return false; DerReader reader(sequence); BytesVector version, privateBytes; bool rsa = false;
    if (!reader.ReadInteger(version) || version.size() != 1 || version[0] != 0 || !ReadAlgorithmIdentifier(reader, rsa) || rsa || !reader.ReadVector(0x04, privateBytes) || privateBytes.empty()) return false;
    DerReader sec1({ privateBytes.data(), privateBytes.size() }); Bytes secSequence; if (!sec1.Read(0x30, secSequence) || !sec1.Empty()) return false; DerReader ec(secSequence); BytesVector ecVersion;
    if (!ec.ReadInteger(ecVersion) || ecVersion.size() != 1 || ecVersion[0] != 1 || !ec.ReadVector(0x04, out.d) || out.d.size() != 32) return false;
    while (!ec.Empty())
    {
        Bytes optional; if (ec.Read(0xa1, optional)) { DerReader bit(optional); BytesVector publicBytes; if (!bit.ReadBitString(publicBytes) || !bit.Empty() || publicBytes.size() != 65 || publicBytes[0] != 4) return false; out.x.assign(publicBytes.begin()+1, publicBytes.begin()+33); out.y.assign(publicBytes.begin()+33, publicBytes.begin()+65); }
        else if (ec.Read(0xa0, optional)) { DerReader oid(optional); Bytes curve; if (!oid.Read(0x06, curve) || !EqualBytes({ curve.data, curve.size }, { 0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07 }) || !oid.Empty()) return false; }
        else return false;
    }
    return !out.d.empty() && out.x.size() == 32 && out.y.size() == 32;
}

BytesVector RsaPrivateDer(const RsaParts& k)
{
    const auto body = Sequence({ IntegerU32(0), Integer(k.n), Integer(k.e), Integer(k.d), Integer(k.p), Integer(k.q), Integer(k.dp), Integer(k.dq), Integer(k.qi) });
    const auto alg = Sequence({ Oid({0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01}), Null() });
    return Sequence({ IntegerU32(0), alg, OctetString(body) });
}
BytesVector RsaPublicDer(const RsaParts& k)
{
    const auto key = Sequence({ Integer(k.n), Integer(k.e) });
    const auto alg = Sequence({ Oid({0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01}), Null() });
    return Sequence({ alg, BitString(key) });
}
BytesVector EccPrivateDer(const EccParts& k)
{
    BytesVector point{4};
    point.insert(point.end(), k.x.begin(), k.x.end());
    point.insert(point.end(), k.y.begin(), k.y.end());
    const auto sec1 = Sequence({
        IntegerU32(1),
        OctetString(k.d),
        Tlv(0xa0, Oid({0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07})),
        Tlv(0xa1, BitString(point))
    });
    const auto alg = Sequence({
        Oid({0x2a,0x86,0x48,0xce,0x3d,0x02,0x01}),
        Oid({0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07})
    });
    return Sequence({ IntegerU32(0), alg, OctetString(sec1) });
}
BytesVector EccPublicDer(const EccParts& k)
{
    BytesVector point{4}; point.insert(point.end(), k.x.begin(), k.x.end()); point.insert(point.end(), k.y.begin(), k.y.end());
    const auto alg = Sequence({ Oid({0x2a,0x86,0x48,0xce,0x3d,0x02,0x01}), Oid({0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07}) });
    return Sequence({ alg, BitString(point) });
}

BytesVector Blob(const BCRYPT_RSAKEY_BLOB& header, std::initializer_list<BytesVector> values)
{
    BytesVector out(sizeof(header)); std::memcpy(out.data(), &header, sizeof(header)); for (const auto& value : values) out.insert(out.end(), value.begin(), value.end()); return out;
}
// CNG key blobs use fixed-width big-endian fields, while parsed DER integers
// keep minimal magnitude. Zero-pad up to the field width; values wider than
// the width pass through untouched so CNG reports the mismatch instead of
// this code silently truncating.
BytesVector Padded(const BytesVector& value, size_t width)
{
    if (value.size() >= width) return value;
    BytesVector padded(width - value.size(), 0);
    padded.insert(padded.end(), value.begin(), value.end());
    return padded;
}
BytesVector RsaPublicBlob(const RsaParts& k)
{
    BCRYPT_RSAKEY_BLOB h{BCRYPT_RSAPUBLIC_MAGIC, static_cast<ULONG>(k.n.size()*8), static_cast<ULONG>(k.e.size()), static_cast<ULONG>(k.n.size()), 0, 0}; return Blob(h, {k.e, k.n});
}
BytesVector RsaPrivateBlob(const RsaParts& k)
{
    // Field widths follow the CNG layout rules: primes and their CRT
    // exponents occupy half the modulus width, d occupies the full modulus
    // width. Minimal-magnitude integers are zero-padded back to those
    // widths; skipping this makes imports fail whenever a component's top
    // byte happens to be zero (intermittent per generated key).
    const size_t modulusBytes = k.n.size();
    const size_t halfBytes = (modulusBytes + 1) / 2;
    BCRYPT_RSAKEY_BLOB h{BCRYPT_RSAFULLPRIVATE_MAGIC, static_cast<ULONG>(modulusBytes*8), static_cast<ULONG>(k.e.size()), static_cast<ULONG>(modulusBytes), static_cast<ULONG>(halfBytes), static_cast<ULONG>(halfBytes)};
    return Blob(h, {k.e, Padded(k.n, modulusBytes), Padded(k.p, halfBytes), Padded(k.q, halfBytes), Padded(k.dp, halfBytes), Padded(k.dq, halfBytes), Padded(k.qi, halfBytes), Padded(k.d, modulusBytes)});
}
BytesVector EccBlob(ULONG magic, const EccParts& k, bool privateKey)
{
    BCRYPT_ECCKEY_BLOB h{magic, 32}; BytesVector out(sizeof(h)); std::memcpy(out.data(), &h, sizeof(h));
    // Coordinates and scalars can be one byte short when the top bit is
    // clear; CNG expects fixed 32-byte fields for P-256.
    for (const BytesVector* value : {&k.x, &k.y}) { const auto padded = Padded(*value, 32); out.insert(out.end(), padded.begin(), padded.end()); }
    if (privateKey) { const auto padded = Padded(k.d, 32); out.insert(out.end(), padded.begin(), padded.end()); }
    return out;
}

bool ExportKey(BCRYPT_KEY_HANDLE key, LPCWSTR type, BytesVector& out)
{
    ULONG size = 0; if (BCryptExportKey(key, nullptr, type, nullptr, 0, &size, 0) != 0 || size == 0 || size > kMaxInputBytes) return false; out.resize(size); if (BCryptExportKey(key, nullptr, type, out.data(), size, &size, 0) != 0) return false; out.resize(size); return true;
}

bool RsaPartsFromPrivate(Bytes privateKey, RsaParts& out) { return ParseRsaPrivateKeyInfo(privateKey, out); }
bool RsaPartsFromPublic(Bytes publicKey, RsaParts& out) { return ParseRsaPublicKeyInfo(publicKey, out); }
bool EccPartsFromPrivate(Bytes privateKey, EccParts& out) { return ParseEccPrivateKeyInfo(privateKey, out); }
bool EccPartsFromPublic(Bytes publicKey, EccParts& out) { return ParseEccPublicKey(publicKey, out); }

struct HashParameters
{
    const wchar_t* name;
    size_t size;
};

const HashParameters* SignatureHash(std::string_view algorithm)
{
    static constexpr HashParameters sha256{BCRYPT_SHA256_ALGORITHM, 32};
    static constexpr HashParameters sha384{BCRYPT_SHA384_ALGORITHM, 48};
    static constexpr HashParameters sha512{BCRYPT_SHA512_ALGORITHM, 64};
    if (algorithm == "rsa-pss-sha256" || algorithm == "rsa-oaep-sha256" || algorithm == "ecdsa-p256-sha256") return &sha256;
    if (algorithm == "rsa-pss-sha384" || algorithm == "rsa-oaep-sha384") return &sha384;
    if (algorithm == "rsa-pss-sha512" || algorithm == "rsa-oaep-sha512") return &sha512;
    return nullptr;
}

ProviderResult<BytesVector> HashForSignature(std::string_view algorithm, Bytes data)
{
    const auto* parameters = SignatureHash(algorithm);
    if (!parameters) return Failure("unsupported asymmetric algorithm");
    AlgHandle alg; if (BCryptOpenAlgorithmProvider(&alg.value, parameters->name, nullptr, 0) != 0) return Failure("signature hash provider initialization failed");
    DWORD objectLength = 0, resultLength = 0;
    if (BCryptGetProperty(alg.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0) return Failure("signature hash provider initialization failed");
    BytesVector object(objectLength), digest(parameters->size); BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(alg.value, &hash, object.data(), objectLength, nullptr, 0, 0) != 0) return Failure("signature hash initialization failed");
    if (data.size && BCryptHashData(hash, const_cast<PUCHAR>(data.data), static_cast<ULONG>(data.size), 0) != 0) { BCryptDestroyHash(hash); return Failure("signature hash operation failed"); }
    const auto status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0); BCryptDestroyHash(hash);
    if (status != 0) return Failure("signature hash operation failed");
    return ProviderResult<BytesVector>::Success(std::move(digest));
}

ProviderResult<KeyHandle> ImportRsaPrivate(const RsaParts& parts)
{
    AlgHandle alg; if(BCryptOpenAlgorithmProvider(&alg.value,BCRYPT_RSA_ALGORITHM,nullptr,0)!=0)return ProviderResult<KeyHandle>::Failure("RSA provider initialization failed"); BytesVector blob=RsaPrivateBlob(parts); KeyHandle key; if(BCryptImportKeyPair(alg.value,nullptr,BCRYPT_RSAFULLPRIVATE_BLOB,&key.value,blob.data(),static_cast<ULONG>(blob.size()),0)!=0)return ProviderResult<KeyHandle>::Failure("invalid RSA private key"); return ProviderResult<KeyHandle>::Success(std::move(key));
}
ProviderResult<KeyHandle> ImportRsaPublic(const RsaParts& parts)
{
    AlgHandle alg; if(BCryptOpenAlgorithmProvider(&alg.value,BCRYPT_RSA_ALGORITHM,nullptr,0)!=0)return ProviderResult<KeyHandle>::Failure("RSA provider initialization failed"); BytesVector blob=RsaPublicBlob(parts); KeyHandle key; if(BCryptImportKeyPair(alg.value,nullptr,BCRYPT_RSAPUBLIC_BLOB,&key.value,blob.data(),static_cast<ULONG>(blob.size()),0)!=0)return ProviderResult<KeyHandle>::Failure("invalid RSA public key"); return ProviderResult<KeyHandle>::Success(std::move(key));
}
ProviderResult<KeyHandle> ImportEccPrivate(const EccParts& parts, bool ecdh)
{
    AlgHandle alg; const auto algorithm=ecdh?BCRYPT_ECDH_P256_ALGORITHM:BCRYPT_ECDSA_P256_ALGORITHM; if(BCryptOpenAlgorithmProvider(&alg.value,algorithm,nullptr,0)!=0)return ProviderResult<KeyHandle>::Failure("P-256 provider initialization failed"); BytesVector blob=EccBlob(ecdh?BCRYPT_ECDH_PRIVATE_P256_MAGIC:BCRYPT_ECDSA_PRIVATE_P256_MAGIC,parts,true); KeyHandle key; if(BCryptImportKeyPair(alg.value,nullptr,BCRYPT_ECCPRIVATE_BLOB,&key.value,blob.data(),static_cast<ULONG>(blob.size()),0)!=0)return ProviderResult<KeyHandle>::Failure("invalid P-256 private key"); return ProviderResult<KeyHandle>::Success(std::move(key));
}
ProviderResult<KeyHandle> ImportEccPublic(const EccParts& parts, bool ecdh)
{
    AlgHandle alg; const auto algorithm=ecdh?BCRYPT_ECDH_P256_ALGORITHM:BCRYPT_ECDSA_P256_ALGORITHM; if(BCryptOpenAlgorithmProvider(&alg.value,algorithm,nullptr,0)!=0)return ProviderResult<KeyHandle>::Failure("P-256 provider initialization failed"); BytesVector blob=EccBlob(ecdh?BCRYPT_ECDH_PUBLIC_P256_MAGIC:BCRYPT_ECDSA_PUBLIC_P256_MAGIC,parts,false); KeyHandle key; if(BCryptImportKeyPair(alg.value,nullptr,BCRYPT_ECCPUBLIC_BLOB,&key.value,blob.data(),static_cast<ULONG>(blob.size()),0)!=0)return ProviderResult<KeyHandle>::Failure("invalid P-256 public key"); return ProviderResult<KeyHandle>::Success(std::move(key));
}
}

ProviderResult<KeyPairBytes> GenerateKeyPair(std::string_view algorithm)
{
    if (algorithm == "rsa-3072")
    {
        AlgHandle alg; KeyHandle key; if(BCryptOpenAlgorithmProvider(&alg.value,BCRYPT_RSA_ALGORITHM,nullptr,0)!=0||BCryptGenerateKeyPair(alg.value,&key.value,3072,0)!=0||BCryptFinalizeKeyPair(key.value,0)!=0)return ProviderResult<KeyPairBytes>::Failure("RSA key generation failed");
        BytesVector privateBlob, publicBlob; if(!ExportKey(key.value,BCRYPT_RSAFULLPRIVATE_BLOB,privateBlob)||!ExportKey(key.value,BCRYPT_RSAPUBLIC_BLOB,publicBlob))return ProviderResult<KeyPairBytes>::Failure("RSA key export failed");
        const auto* ph=reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(privateBlob.data()); size_t pos=sizeof(*ph); RsaParts parts; auto take=[&](size_t n){BytesVector v(privateBlob.begin()+pos,privateBlob.begin()+pos+n);pos+=n;return v;}; parts.e=take(ph->cbPublicExp);parts.n=take(ph->cbModulus);parts.p=take(ph->cbPrime1);parts.q=take(ph->cbPrime2);parts.dp=take(ph->cbPrime1);parts.dq=take(ph->cbPrime2);parts.qi=take(ph->cbPrime1);parts.d=take(ph->cbModulus); return ProviderResult<KeyPairBytes>::Success({"rsa-3072",RsaPrivateDer(parts),RsaPublicDer(parts)});
    }
    if (algorithm == "p256")
    {
        AlgHandle alg; KeyHandle key; if(BCryptOpenAlgorithmProvider(&alg.value,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0)!=0||BCryptGenerateKeyPair(alg.value,&key.value,256,0)!=0||BCryptFinalizeKeyPair(key.value,0)!=0)return ProviderResult<KeyPairBytes>::Failure("P-256 key generation failed"); BytesVector privateBlob, publicBlob; if(!ExportKey(key.value,BCRYPT_ECCPRIVATE_BLOB,privateBlob)||!ExportKey(key.value,BCRYPT_ECCPUBLIC_BLOB,publicBlob))return ProviderResult<KeyPairBytes>::Failure("P-256 key export failed"); const auto* privateHeader=reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(privateBlob.data()); const auto* publicHeader=reinterpret_cast<const BCRYPT_ECCKEY_BLOB*>(publicBlob.data()); if(privateBlob.size()<sizeof(*privateHeader)+32||publicBlob.size()<sizeof(*publicHeader)+64||privateHeader->cbKey!=32||publicHeader->cbKey!=32)return ProviderResult<KeyPairBytes>::Failure("invalid generated P-256 key"); EccParts parts; parts.x.assign(privateBlob.begin()+sizeof(*privateHeader),privateBlob.begin()+sizeof(*privateHeader)+32); parts.y.assign(privateBlob.begin()+sizeof(*privateHeader)+32,privateBlob.begin()+sizeof(*privateHeader)+64); parts.d.assign(privateBlob.begin()+sizeof(*privateHeader)+64,privateBlob.begin()+sizeof(*privateHeader)+96); return ProviderResult<KeyPairBytes>::Success({"p256",EccPrivateDer(parts),EccPublicDer(parts)});
    }
    return ProviderResult<KeyPairBytes>::Failure("unsupported key pair algorithm");
}

// --- ECDSA signature interop helpers -------------------------------------
// CNG produces/consumes raw fixed-width r||s pairs, but the interoperable
// encoding for ECDSA signatures is the ASN.1 DER ECDSA-Sig-Value
// SEQUENCE { INTEGER r, INTEGER s } used by OpenSSL/WebCrypto.

// Converts a raw big-endian integer of half bytes into a minimal,
// signed-INTEGER-encoded byte string (leading zeros stripped, 0x00 inserted
// when the high bit would make it look negative).
static BytesVector DerIntegerFromRaw(const unsigned char* p, size_t half)
{
    size_t i = 0;
    while (i < half && p[i] == 0) ++i;
    BytesVector v(p + i, p + half);
    if (v.empty())
        v.push_back(0x00);
    else if (v[0] & 0x80)
        v.insert(v.begin(), 0x00);
    return v;
}

// Converts a raw r||s pair into DER. Returns an empty vector when the input
// is not an even-sized raw pair.
static BytesVector EcdsaRawToDer(const unsigned char* raw, size_t size)
{
    if (size == 0 || (size % 2) != 0)
        return BytesVector();
    const size_t half = size / 2;
    const BytesVector r = DerIntegerFromRaw(raw, half);
    const BytesVector s = DerIntegerFromRaw(raw + half, half);
    const size_t body = 2 + r.size() + 2 + s.size();
    BytesVector der;
    der.reserve(body + 2);
    der.push_back(0x30);
    der.push_back(static_cast<unsigned char>(body));
    der.push_back(0x02);
    der.push_back(static_cast<unsigned char>(r.size()));
    der.insert(der.end(), r.begin(), r.end());
    der.push_back(0x02);
    der.push_back(static_cast<unsigned char>(s.size()));
    der.insert(der.end(), s.begin(), s.end());
    return der;
}

// Parses a DER ECDSA-Sig-Value back into a fixed-width raw r||s pair.
// Accepts only well-formed encodings; returns false otherwise.
static bool EcdsaDerToRaw(const unsigned char* data, size_t size, unsigned char* out, size_t half)
{
    size_t pos = 0;
    if (size < 6 || data[pos++] != 0x30)
        return false;
    size_t body = data[pos++];
    if (body & 0x80)
    {
        const size_t n = body & 0x7f;
        if (n > 2 || pos + n > size)
            return false;
        body = 0;
        for (size_t i = 0; i < n; ++i)
            body = (body << 8) | data[pos++];
    }
    if (body != size - pos)
        return false;
    std::memset(out, 0, half * 2);
    for (int side = 0; side < 2; ++side)
    {
        if (pos + 2 > size || data[pos] != 0x02)
            return false;
        const size_t totalLen = data[pos + 1];
        pos += 2;
        if (totalLen == 0 || totalLen > half + 1 || pos + totalLen > size)
            return false;
        const unsigned char* v = data + pos;
        size_t vlen = totalLen;
        // A leading 0x00 is allowed only to mark the value positive.
        if (v[0] == 0x00)
        {
            ++v;
            --vlen;
        }
        else if (v[0] & 0x80)
        {
            return false; // negative INTEGER is invalid here
        }
        if (vlen == 0 || vlen > half)
            return false;
        std::memcpy(out + side * half + (half - vlen), v, vlen);
        pos += totalLen; // skip the FULL original content incl. pad byte
    }
    return pos == size;
}
ProviderResult<BytesVector> Sign(std::string_view algorithm, Bytes privateKey, Bytes data)
{
    auto digest=HashForSignature(algorithm, data); if(!digest.ok)return digest;
    KeyHandle key; if(algorithm.rfind("rsa-pss-sha", 0) == 0){const auto* hp=SignatureHash(algorithm); if(!hp)return Failure("unsupported RSA-PSS algorithm"); RsaParts parts;if(!RsaPartsFromPrivate(privateKey,parts))return Failure("invalid RSA private key");auto imported=ImportRsaPrivate(parts);if(!imported.ok)return Failure(imported.error.message);key=std::move(imported.value);BCRYPT_PSS_PADDING_INFO info{hp->name,static_cast<ULONG>(hp->size)};ULONG size=0;if(BCryptSignHash(key.value,&info,digest.value.data(),static_cast<ULONG>(digest.value.size()),nullptr,0,&size,BCRYPT_PAD_PSS)!=0||size>kMaxOutputBytes)return Failure("RSA-PSS signing failed");BytesVector sig(size);if(BCryptSignHash(key.value,&info,digest.value.data(),static_cast<ULONG>(digest.value.size()),sig.data(),size,&size,BCRYPT_PAD_PSS)!=0)return Failure("RSA-PSS signing failed");sig.resize(size);return ProviderResult<BytesVector>::Success(std::move(sig));}
    if(algorithm=="ecdsa-p256-sha256"){EccParts parts;if(!EccPartsFromPrivate(privateKey,parts))return Failure("invalid P-256 private key");auto imported=ImportEccPrivate(parts,false);if(!imported.ok)return Failure(imported.error.message);key=std::move(imported.value);ULONG size=0;if(BCryptSignHash(key.value,nullptr,digest.value.data(),32,nullptr,0,&size,0)!=0||size>kMaxOutputBytes)return Failure("ECDSA signing failed");BytesVector sig(size);if(BCryptSignHash(key.value,nullptr,digest.value.data(),32,sig.data(),size,&size,0)!=0)return Failure("ECDSA signing failed");sig.resize(size);auto der=EcdsaRawToDer(sig.data(),sig.size());if(der.empty())return Failure("ECDSA signature encoding failed");return ProviderResult<BytesVector>::Success(std::move(der));}
    return Failure("unsupported signature algorithm");
}

ProviderResult<bool> Verify(std::string_view algorithm, Bytes publicKey, Bytes data, Bytes signature)
{
    auto digest=HashForSignature(algorithm, data); if(!digest.ok)return ProviderResult<bool>::Failure(digest.error.message);
    if(algorithm.rfind("rsa-pss-sha", 0) == 0){const auto* hp=SignatureHash(algorithm); if(!hp)return ProviderResult<bool>::Failure("unsupported RSA-PSS algorithm"); RsaParts parts;if(!RsaPartsFromPublic(publicKey,parts))return ProviderResult<bool>::Failure("invalid RSA public key");auto imported=ImportRsaPublic(parts);if(!imported.ok)return ProviderResult<bool>::Failure(imported.error.message);BCRYPT_PSS_PADDING_INFO info{hp->name,static_cast<ULONG>(hp->size)};const auto status=BCryptVerifySignature(imported.value.value,&info,digest.value.data(),static_cast<ULONG>(digest.value.size()),const_cast<PUCHAR>(signature.data),static_cast<ULONG>(signature.size),BCRYPT_PAD_PSS);return ProviderResult<bool>::Success(status==0);}
    if(algorithm=="ecdsa-p256-sha256"){EccParts parts;if(!EccPartsFromPublic(publicKey,parts))return ProviderResult<bool>::Failure("invalid P-256 public key");auto imported=ImportEccPublic(parts,false);if(!imported.ok)return ProviderResult<bool>::Failure(imported.error.message);unsigned char rawSig[64];const unsigned char* sigPtr;size_t sigSize;if(signature.size==64){sigPtr=signature.data;sigSize=64;}else if(EcdsaDerToRaw(signature.data,signature.size,rawSig,32)){sigPtr=rawSig;sigSize=64;}else return ProviderResult<bool>::Failure("invalid ECDSA signature encoding");const auto status=BCryptVerifySignature(imported.value.value,nullptr,digest.value.data(),32,const_cast<PUCHAR>(sigPtr),static_cast<ULONG>(sigSize),0);return ProviderResult<bool>::Success(status==0);}
    return ProviderResult<bool>::Failure("unsupported signature algorithm");
}

ProviderResult<BytesVector> Encrypt(std::string_view algorithm, Bytes publicKey, Bytes plaintext)
{
    const auto* hp=SignatureHash(algorithm); if(!hp || algorithm.rfind("rsa-oaep-sha", 0) != 0)return Failure("unsupported encryption algorithm"); RsaParts parts;if(!RsaPartsFromPublic(publicKey,parts))return Failure("invalid RSA public key");auto imported=ImportRsaPublic(parts);if(!imported.ok)return Failure(imported.error.message);BCRYPT_OAEP_PADDING_INFO info{hp->name,nullptr,0};ULONG size=0;auto status=BCryptEncrypt(imported.value.value,const_cast<PUCHAR>(plaintext.data),static_cast<ULONG>(plaintext.size),&info,nullptr,0,nullptr,0,&size,BCRYPT_PAD_OAEP);if(status!=0||size>kMaxOutputBytes)return Failure("RSA-OAEP encryption failed");BytesVector output(size);if(BCryptEncrypt(imported.value.value,const_cast<PUCHAR>(plaintext.data),static_cast<ULONG>(plaintext.size),&info,nullptr,0,output.data(),size,&size,BCRYPT_PAD_OAEP)!=0)return Failure("RSA-OAEP encryption failed");output.resize(size);return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Decrypt(std::string_view algorithm, Bytes privateKey, Bytes ciphertext)
{
    const auto* hp=SignatureHash(algorithm); if(!hp || algorithm.rfind("rsa-oaep-sha", 0) != 0)return Failure("unsupported encryption algorithm"); RsaParts parts;if(!RsaPartsFromPrivate(privateKey,parts))return Failure("invalid RSA private key");auto imported=ImportRsaPrivate(parts);if(!imported.ok)return Failure(imported.error.message);BCRYPT_OAEP_PADDING_INFO info{hp->name,nullptr,0};ULONG size=0;if(BCryptDecrypt(imported.value.value,const_cast<PUCHAR>(ciphertext.data),static_cast<ULONG>(ciphertext.size),&info,nullptr,0,nullptr,0,&size,BCRYPT_PAD_OAEP)!=0||size>kMaxOutputBytes)return Failure("RSA-OAEP decryption failed");BytesVector output(size);if(BCryptDecrypt(imported.value.value,const_cast<PUCHAR>(ciphertext.data),static_cast<ULONG>(ciphertext.size),&info,nullptr,0,output.data(),size,&size,BCRYPT_PAD_OAEP)!=0)return Failure("RSA-OAEP decryption failed");output.resize(size);return ProviderResult<BytesVector>::Success(std::move(output));
}

ProviderResult<BytesVector> Derive(std::string_view algorithm, Bytes privateKey, Bytes peerPublicKey)
{
    if(algorithm!="ecdh-p256")return Failure("unsupported key agreement algorithm"); EccParts privateParts,publicParts;if(!EccPartsFromPrivate(privateKey,privateParts)||!EccPartsFromPublic(peerPublicKey,publicParts))return Failure("invalid P-256 key");auto priv=ImportEccPrivate(privateParts,true);auto pub=ImportEccPublic(publicParts,true);if(!priv.ok||!pub.ok)return Failure(!priv.ok?priv.error.message:pub.error.message);SecretHandle secret;if(BCryptSecretAgreement(priv.value.value,pub.value.value,&secret.value,0)!=0)return Failure("ECDH agreement failed");ULONG size=0;if(BCryptDeriveKey(secret.value,BCRYPT_KDF_RAW_SECRET,nullptr,nullptr,0,&size,0)!=0||size==0||size>kMaxOutputBytes)return Failure("ECDH derivation failed");BytesVector output(size);if(BCryptDeriveKey(secret.value,BCRYPT_KDF_RAW_SECRET,nullptr,output.data(),size,&size,0)!=0)return Failure("ECDH derivation failed");output.resize(size);/* BCRYPT_KDF_RAW_SECRET yields little-endian bytes; reverse to the standard big-endian shared secret used by OpenSSL/WebCrypto. */std::reverse(output.begin(),output.end());return ProviderResult<BytesVector>::Success(std::move(output));
}
}
#else
#include "CryptoProvider.hpp"
namespace lodecrypto { }
#endif
