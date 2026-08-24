
-- Regression coverage for the native crypto module.
local sanity = require("../init")
local describe = sanity.describe
local it = sanity.it
local expect = sanity.expect
local crypto = require("../../crypto")

local function text(data: buffer): string return buffer.tostring(data) end
local function hex(data: buffer): string return text(crypto.hexEncode(data)) end
local function bufferLength(data: buffer): number return buffer.len(data) end
local function bufferFromHex(text: string): buffer return crypto.hexDecode(text) end

local signatureAlgorithms: {crypto.SignatureAlgorithm} = {"rsa-pss-sha256", "rsa-pss-sha384", "rsa-pss-sha512"}
local encryptionAlgorithms: {crypto.EncryptionAlgorithm} = {"rsa-oaep-sha256", "rsa-oaep-sha384", "rsa-oaep-sha512"}

describe("crypto encoding", function()
    it("encodes and decodes hexadecimal and Base64", function()
        local data = bufferFromHex("000102feff")
        expect(hex(data)).toBe("000102feff")
        expect(hex(crypto.hexDecode("A0b1"))).toBe("a0b1")
        expect(hex(crypto.hexDecode(crypto.hexEncode(data)))).toBe("000102feff")
        expect(text(crypto.base64Encode(data))).toBe("AAEC/v8=")
        expect(hex(crypto.base64Decode("AAEC/v8="))).toBe("000102feff")
        expect(hex(crypto.base64Decode(crypto.base64Encode(data)))).toBe("000102feff")
        expect(text(crypto.base64Encode("hello"))).toBe("aGVsbG8=")
        local pem = crypto.pemEncode("TEST DATA", data)
        expect(hex(crypto.pemDecode(pem, "TEST DATA"))).toBe(hex(data))
        expect(pcall(function() crypto.pemDecode(buffer.tostring(pem) .. "x", "TEST DATA") end)).toBe(false)
        expect(pcall(function() crypto.hexDecode("abc") end)).toBe(false)
        expect(pcall(function() crypto.hexDecode(bufferFromHex("00")) end)).toBe(false)
        expect(pcall(function() crypto.hexDecode("zz") end)).toBe(false)
        expect(pcall(function() crypto.base64Decode("aGVsbG8") end)).toBe(false)
    end)
end)

describe("crypto hashes", function()
    it("matches SHA-2 vectors", function()
        expect(hex(crypto.hash("sha256", ""))).toBe("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
        expect(hex(crypto.hash("sha256", "abc"))).toBe("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        expect(hex(crypto.hash("sha384", "abc"))).toBe("cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")
        expect(hex(crypto.hash("sha512", "abc"))).toBe("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f")
    end)
    it("accepts strings and buffers equally", function()
        expect(hex(crypto.hash("sha256", "binary\0data"))).toBe(hex(crypto.hash("sha256", bufferFromHex("62696e6172790064617461"))))
    end)
    it("rejects unsupported algorithms and invalid inputs", function()
        expect(pcall(function() (crypto.hash :: any)("md5", "x") end)).toBe(false)
        expect(pcall(function() crypto.hash("sha256", {}) end)).toBe(false)
        expect(pcall(function() crypto.randomBytes(-1) end)).toBe(false)
        expect(pcall(function() crypto.randomBytes(1.5) end)).toBe(false)
        expect(pcall(function() crypto.base64Decode("A===") end)).toBe(false)
        expect(pcall(function() crypto.base64Decode("AA=A") end)).toBe(false)
        expect(pcall(function() crypto.base64Decode("AB==") end)).toBe(false)
        expect(pcall(function() crypto.base64Decode("AAA") end)).toBe(false)
    end)
end)

describe("crypto HMAC and equality", function()
    it("matches the RFC 4231 SHA-256 vector", function()
        local key = string.rep(string.char(0x0b), 20)
        expect(hex(crypto.hmac("sha256", key, "Hi There"))).toBe("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7")
    end)
    it("compares binary values and generates random bytes", function()
        expect(crypto.timingSafeEqual("abc", "abc")).toBe(true)
        expect(crypto.timingSafeEqual("abc", "abd")).toBe(false)
        expect(crypto.timingSafeEqual("abc", "ab")).toBe(false)
        expect(crypto.timingSafeEqual(string.rep("\0", 256), "")).toBe(false)
        local random = crypto.randomBytes(32)
        expect(bufferLength(random)).toBe(32)
        expect(crypto.timingSafeEqual(random, random)).toBe(true)
    end)
end)

describe("crypto AES-GCM and KDFs", function()
    it("supports HKDF and PBKDF2 vectors", function()
        expect(hex(crypto.hkdf("sha256", bufferFromHex(string.rep("0b", 22)), 42, bufferFromHex("000102030405060708090a0b0c"), bufferFromHex("f0f1f2f3f4f5f6f7f8f9")))).toBe("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")
        expect(hex(crypto.pbkdf2("sha256", "password", "salt", 1, 32))).toBe("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b")
        expect(hex(crypto.hkdf("sha256", "ikm", 32))).toHaveLength(64)
    end)
    it("round-trips authenticated data", function()
        local key = bufferFromHex(string.rep("00", 32))
        local nonce = bufferFromHex(string.rep("00", 12))
        local encrypted = crypto.aeadEncrypt("aes-256-gcm", key, nonce, "secret", "header")
        expect(hex(crypto.aeadDecrypt("aes-256-gcm", key, nonce, encrypted, "header"))).toBe("736563726574")
        expect(pcall(function() crypto.aeadDecrypt("aes-256-gcm", key, nonce, encrypted, "wrong") end)).toBe(false)
        local tampered = buffer.create(buffer.len(encrypted))
        buffer.copy(tampered, 0, encrypted, 0, buffer.len(encrypted))
        buffer.writeu8(tampered, 0, bit32.bxor(buffer.readu8(tampered, 0), 1))
        expect(pcall(function() crypto.aeadDecrypt("aes-256-gcm", key, nonce, tampered, "header") end)).toBe(false)
    end)
end)

describe("crypto asymmetric", function()
    it("signs and verifies with P-256", function()
        local pair = crypto.generateKeyPair("p256")
        expect(pair.algorithm).toBe("p256")
        local signature = crypto.sign("ecdsa-p256-sha256", pair.privateKey, "message")
        expect(crypto.verify("ecdsa-p256-sha256", pair.publicKey, "message", signature)).toBe(true)
        expect(crypto.verify("ecdsa-p256-sha256", pair.publicKey, "other", signature)).toBe(false)
    end)
    it("signs and encrypts with RSA-3072", function()
        local pair = crypto.generateKeyPair("rsa-3072")
        for _, algorithm in ipairs(signatureAlgorithms) do
            -- TEMPORARY diagnostics: capture the failing key material so the
            -- intermittent "invalid RSA private key" can be reproduced offline.
            local okSign, signResult = pcall(crypto.sign, algorithm, pair.privateKey, "message")
            if not okSign then
                print("DIAG_RSA_BEGIN algorithm=" .. algorithm)
                print(tostring(pair.privateKey))
                print("DIAG_RSA_END")
                error(tostring(signResult))
            end
            local signature = signResult
            expect(crypto.verify(algorithm, pair.publicKey, "message", signature)).toBe(true)
            expect(crypto.verify(algorithm, pair.publicKey, "other", signature)).toBe(false)
        end
        for _, algorithm in ipairs(encryptionAlgorithms) do
            local encrypted = crypto.encrypt(algorithm, pair.publicKey, "secret")
            expect(hex(crypto.decrypt(algorithm, pair.privateKey, encrypted))).toBe("736563726574")
        end
    end)
    it("derives matching P-256 ECDH secrets", function()
        local alice = crypto.generateKeyPair("p256")
        local bob = crypto.generateKeyPair("p256")
        local left = crypto.derive("ecdh-p256", alice.privateKey, bob.publicKey)
        local right = crypto.derive("ecdh-p256", bob.privateKey, alice.publicKey)
        expect(crypto.timingSafeEqual(left, right)).toBe(true)
    end)
end)
