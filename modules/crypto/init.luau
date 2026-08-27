--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Binary data accepted by crypto operations.
export type ByteInput = string | buffer

--- Hash algorithms exposed by the platform crypto provider.
export type HashAlgorithm = "sha256" | "sha384" | "sha512"

--- Authenticated-encryption algorithms.
export type AeadAlgorithm = "aes-256-gcm"

--- Supported generated key families.
export type KeyPairAlgorithm = "rsa-3072" | "p256"

--- Supported signature schemes. Signatures use ASN.1 DER encoding.
export type SignatureAlgorithm = "rsa-pss-sha256" | "rsa-pss-sha384" | "rsa-pss-sha512" | "ecdsa-p256-sha256"

--- Supported public-key encryption schemes.
export type EncryptionAlgorithm = "rsa-oaep-sha256" | "rsa-oaep-sha384" | "rsa-oaep-sha512"

--- Supported key-agreement schemes.
export type DeriveAlgorithm = "ecdh-p256"

--- PKCS#8/SPKI key pair returned by generateKeyPair.
export type KeyPair = {
    --- Algorithm used to generate the key pair.
    algorithm: KeyPairAlgorithm,
    --- Unencrypted PKCS#8 DER private key.
    privateKey: buffer,
    --- SubjectPublicKeyInfo DER public key.
    publicKey: buffer,
}

--[[
    Native cryptography module.

    Binary inputs accept strings or buffers. Binary outputs are buffers so NUL
    bytes are preserved. Operations are synchronous and stateless. The native
    provider is BCrypt/CNG on Windows and OpenSSL on Linux/macOS.

    Asymmetric private keys must be unencrypted PKCS#8 DER. Public keys must be
    SubjectPublicKeyInfo DER. RSA signatures use RSA-PSS with a salt matching
    the selected digest; ECDSA signatures use ASN.1 DER. ECDH returns the
    standard big-endian raw shared secret.
]]
export type CryptoModule = {
    --- Generates cryptographically secure random bytes.
    --- @param length Integer from 0 through 1 MiB.
    --- @return A buffer containing exactly length bytes.
    randomBytes: (length: number) -> buffer,

    --- Encodes data as lowercase hexadecimal bytes.
    --- @param data String or buffer input.
    --- @return A buffer containing lowercase hexadecimal ASCII bytes.
    hexEncode: (data: ByteInput) -> buffer,

    --- Decodes an even-length hexadecimal input.
    --- @param text Lowercase or uppercase hexadecimal string or buffer.
    --- @return Decoded bytes.
    hexDecode: (text: ByteInput) -> buffer,

    --- Encodes data as padded standard Base64 bytes.
    --- @param data String or buffer input.
    --- @return A buffer containing standard Base64 ASCII bytes.
    base64Encode: (data: ByteInput) -> buffer,

    --- Decodes strictly validated padded standard Base64 input.
    --- @param text Standard Base64 string or buffer.
    --- @return Decoded bytes.
    base64Decode: (text: ByteInput) -> buffer,

    --- Encodes data as unpadded URL-safe Base64 ('-' and '_' alphabet).
    --- @param data String or buffer input.
    --- @return A buffer containing URL-safe Base64 ASCII bytes.
    base64urlEncode: (data: ByteInput) -> buffer,

    --- Decodes padded or unpadded URL-safe Base64 input.
    --- @param text URL-safe Base64 string or buffer.
    --- @return Decoded bytes.
    base64urlDecode: (text: ByteInput) -> buffer,

    --- Generates a random v4 UUID string.
    --- @return A canonically formatted lowercase UUID.
    randomUUID: () -> string,

    --- Generates a random integer in [min, max] using the CSPRNG.
    --- @param min Minimum value (defaults to 0).
    --- @param max Maximum value (defaults to math.maxinteger).
    --- @return A random integer in the closed interval.
    randomInt: (min: number?, max: number?) -> number,

    --- Encodes DER bytes as PEM with the supplied label.
    --- @param label PEM label such as PUBLIC KEY or PRIVATE KEY.
    --- @param der DER-encoded bytes.
    --- @return A buffer containing PEM ASCII bytes with 64-character Base64 lines.
    pemEncode: (label: string, der: ByteInput) -> buffer,

    --- Decodes a strict PEM block with the expected label.
    --- @param pem PEM string or buffer containing one block and no trailing data.
    --- @param label Expected PEM label.
    --- @return Decoded DER bytes.
    pemDecode: (pem: ByteInput, label: string) -> buffer,

    --- Compares byte sequences without early exit on byte differences.
    --- @param left First string or buffer.
    --- @param right Second string or buffer.
    --- @return Whether lengths and all bytes are equal.
    timingSafeEqual: (left: ByteInput, right: ByteInput) -> boolean,

    --- Computes a SHA-2 digest.
    --- @param algorithm sha256, sha384, or sha512.
    --- @param data String or buffer input.
    --- @return Digest bytes.
    hash: (algorithm: string, data: ByteInput) -> buffer,

    --- Computes an HMAC.
    --- @param algorithm sha256, sha384, or sha512.
    --- @param key Secret key bytes.
    --- @param data Message bytes.
    --- @return MAC bytes.
    hmac: (algorithm: string, key: ByteInput, data: ByteInput) -> buffer,

    --- Derives key material with HKDF.
    --- @param algorithm sha256, sha384, or sha512.
    --- @param ikm Input key material.
    --- @param length Output length, subject to HKDF and module limits.
    --- @param salt Optional salt; omitted salt uses a zero hash-length salt.
    --- @param info Optional context information.
    --- @return Derived key material.
    hkdf: (algorithm: string, ikm: ByteInput, length: number, salt: ByteInput?, info: ByteInput?) -> buffer,

    --- Derives key material with PBKDF2-HMAC.
    --- @param algorithm sha256, sha384, or sha512.
    --- @param password Password bytes.
    --- @param salt Salt bytes.
    --- @param iterations Integer from 1 through 1,000,000.
    --- @param length Output length within the module limit.
    --- @return Derived key material.
    pbkdf2: (algorithm: string, password: ByteInput, salt: ByteInput, iterations: number, length: number) -> buffer,

    --- Encrypts and authenticates with AES-256-GCM.
    --- @param algorithm Only aes-256-gcm.
    --- @param key Exactly 32 bytes.
    --- @param nonce Exactly 12 bytes; callers must never reuse a nonce with a key.
    --- @param plaintext Plaintext bytes.
    --- @param aad Optional additional authenticated data.
    --- @return Ciphertext followed by a 16-byte authentication tag.
    aeadEncrypt: (algorithm: string, key: ByteInput, nonce: ByteInput, plaintext: ByteInput, aad: ByteInput?) -> buffer,

    --- Authenticates and decrypts AES-256-GCM.
    --- @param algorithm Only aes-256-gcm.
    --- @param key Exactly 32 bytes.
    --- @param nonce Exactly 12 bytes.
    --- @param ciphertextAndTag Ciphertext followed by a 16-byte tag.
    --- @param aad Optional additional authenticated data.
    --- @return Authenticated plaintext.
    aeadDecrypt: (algorithm: string, key: ByteInput, nonce: ByteInput, ciphertextAndTag: ByteInput, aad: ByteInput?) -> buffer,

    --- Generates an RSA-3072 or P-256 key pair.
    --- @param algorithm rsa-3072 or p256.
    --- @return Unencrypted PKCS#8 private and SPKI public keys.
    generateKeyPair: (algorithm: string) -> KeyPair,

    --- Signs data with RSA-PSS SHA-2 variants or ECDSA-P256-SHA256.
    --- @param algorithm Signature scheme.
    --- @param privateKey Unencrypted PKCS#8 DER private key.
    --- @param data Data to sign.
    --- @return ASN.1 DER signature bytes.
    sign: (algorithm: string, privateKey: ByteInput, data: ByteInput) -> buffer,

    --- Verifies a signature.
    --- @param algorithm Signature scheme.
    --- @param publicKey SPKI DER public key.
    --- @param data Signed data.
    --- @param signature ASN.1 DER signature bytes.
    --- @return True for a valid signature; false for an invalid signature.
    verify: (algorithm: string, publicKey: ByteInput, data: ByteInput, signature: ByteInput) -> boolean,

    --- Encrypts with RSA-OAEP using SHA-256, SHA-384, or SHA-512.
    --- @param algorithm Only rsa-oaep-sha256.
    --- @param publicKey RSA SPKI DER public key.
    --- @param plaintext RSA-OAEP plaintext within the key-size limit.
    --- @return RSA ciphertext.
    encrypt: (algorithm: string, publicKey: ByteInput, plaintext: ByteInput) -> buffer,

    --- Decrypts RSA-OAEP-SHA256 ciphertext.
    --- @param algorithm Only rsa-oaep-sha256.
    --- @param privateKey RSA PKCS#8 DER private key.
    --- @param ciphertext RSA ciphertext.
    --- @return Decrypted plaintext.
    decrypt: (algorithm: string, privateKey: ByteInput, ciphertext: ByteInput) -> buffer,

    --- Performs P-256 ECDH key agreement.
    --- @param algorithm Only ecdh-p256.
    --- @param privateKey P-256 PKCS#8 DER private key.
    --- @param peerPublicKey P-256 SPKI DER public key.
    --- @return Raw shared secret bytes.
    derive: (algorithm: string, privateKey: ByteInput, peerPublicKey: ByteInput) -> buffer,
}

return {} :: CryptoModule
