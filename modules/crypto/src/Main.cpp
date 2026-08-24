// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "CryptoProvider.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/StackValue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace lodecrypto
{
namespace
{
struct Input
{
    // Owns a copy of the bytes: the source Value may be a temporary (from
    // StackValue::ToValue), so pointing into its string/buffer would be a
    // use-after-free after the statement ends.
    std::vector<uint8_t> owned;
    Bytes bytes{};
    bool valid = false;
};

Input ReadBytes(const Lode::Value& value)
{
    Input out;
    if (value.IsString())
    {
        const auto view = value.AsStringView();
        out.owned.assign(reinterpret_cast<const uint8_t*>(view.data()), reinterpret_cast<const uint8_t*>(view.data()) + view.size());
        out.bytes = { out.owned.data(), out.owned.size() };
        out.valid = true;
        return out;
    }
    if (value.IsBuffer())
    {
        const auto span = value.AsSpan();
        out.owned.assign(span.begin(), span.end());
        out.bytes = { out.owned.data(), out.owned.size() };
        out.valid = true;
        return out;
    }
    return {};
}

bool ReadInteger(const Lode::Value& value, uint64_t maximum, uint64_t& output)
{
    if (!value.IsNumber()) return false;
    const double number = value.AsNumber();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number || number > static_cast<double>(maximum)) return false;
    output = static_cast<uint64_t>(number);
    return true;
}

Lode::Value BytesValue(Lode::State& vm, const std::vector<uint8_t>& bytes)
{
    Lode::Value output = vm.CreateBuffer(bytes.size());
    if (!output.IsBuffer()) return output;
    auto span = output.AsSpan();
    if (!bytes.empty() && span.size() == bytes.size()) std::memcpy(span.data(), bytes.data(), bytes.size());
    return output;
}

Lode::Value Error(Lode::State& vm, const std::string& operation, const std::string& message)
{
    vm.RaiseError("crypto " + operation + ": " + message);
    return Lode::Value();
}

bool RequireBytes(Lode::State& vm, Lode::StackArgs args, size_t index, const char* operation, Input& output)
{
    if (index >= args.Size()) { Error(vm, operation, "expected binary input"); return false; }
    // Keep the converted Value alive for the duration of the copy.
    const Lode::Value value = args[index].ToValue();
    output = ReadBytes(value);
    if (!output.valid) { Error(vm, operation, "expected string or buffer"); return false; }
    if (output.bytes.size > kMaxInputBytes) { Error(vm, operation, "input exceeds limit"); return false; }
    return true;
}

bool RequireAlgorithm(Lode::State& vm, Lode::StackArgs args, size_t index, const char* operation, std::string& output)
{
    if (index >= args.Size() || !args[index].IsString()) { Error(vm, operation, "algorithm must be a string"); return false; }
    output = args[index].AsString();
    return true;
}

}
} // namespace lodecrypto

using namespace lodecrypto;

LODE_MODULE(vm)
{
    Lode::Exports exports(vm);

    exports.Function("randomBytes", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        uint64_t size = 0;
        if (args.empty() || !ReadInteger(args[0].ToValue(), kMaxRandomBytes, size)) return Error(vm, "randomBytes", "length must be a finite non-negative integer within limit");
        auto result = RandomBytes(static_cast<size_t>(size));
        if (!result.ok) return Error(vm, "randomBytes", result.error.message);
        return BytesValue(vm, result.value);
    });

    exports.Function("hexEncode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm,args,0,"hexEncode",input)) return {};
        auto result = HexEncode(input.bytes);
        if (!result.ok) return Error(vm, "hexEncode", result.error.message);
        return BytesValue(vm, result.value);
    });
    exports.Function("hexDecode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm, args, 0, "hexDecode", input)) return {};
        if (input.bytes.size > kMaxOutputBytes * 2) return Error(vm, "hexDecode", "input exceeds limit");
        auto result=HexDecode(input.bytes); if(!result.ok)return Error(vm,"hexDecode",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("base64Encode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm,args,0,"base64Encode",input)) return {};
        auto result = Base64Encode(input.bytes);
        if (!result.ok) return Error(vm, "base64Encode", result.error.message);
        return BytesValue(vm, result.value);
    });
    exports.Function("base64Decode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm, args, 0, "base64Decode", input)) return {};
        if (input.bytes.size > ((kMaxOutputBytes + 2) / 3) * 4) return Error(vm, "base64Decode", "input exceeds limit");
        auto result=Base64Decode(input.bytes); if(!result.ok)return Error(vm,"base64Decode",result.error.message); return BytesValue(vm,result.value);
    });
    // --- base64url / randomUUID / randomInt ---------------------------------
    exports.Function("base64urlEncode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm,args,0,"base64urlEncode",input)) return {};
        auto result = Base64Encode(input.bytes);
        if (!result.ok) return Error(vm, "base64urlEncode", result.error.message);
        std::string out;
        for (unsigned char ch : result.value) {
            if (ch == '+') out += '-';
            else if (ch == '/') out += '_';
            else if (ch == '=') continue; // unpadded alphabet
            else out += static_cast<char>(ch);
        }
        return Lode::Value(out);
    });
    exports.Function("base64urlDecode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input input; if (!RequireBytes(vm, args, 0, "base64urlDecode", input)) return {};
        std::string text(reinterpret_cast<const char*>(input.bytes.data), input.bytes.size);
        std::string norm;
        norm.reserve(text.size() + 3);
        for (char ch : text) {
            if (ch == '-') norm += '+';
            else if (ch == '_') norm += '/';
            else if (ch == '=') { /* reject below via strict decode */ }
            else norm += ch;
        }
        while (norm.size() % 4 != 0) norm += '=';
        Bytes padded{ reinterpret_cast<const unsigned char*>(norm.data()), norm.size() };
        auto result = Base64Decode(padded);
        if (!result.ok) return Error(vm, "base64urlDecode", result.error.message);
        return BytesValue(vm, result.value);
    });
    exports.Function("randomUUID", [](Lode::State& vm, Lode::StackArgs) -> Lode::Value {
        auto r = RandomBytes(16);
        if (!r.ok) return Error(vm, "randomUUID", r.error.message);
        auto& b = r.value;
        b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40); // version 4
        b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80); // RFC 4122 variant
        char buf[37];
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return Lode::Value(std::string(buf));
    });
    exports.Function("randomInt", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsNumber() || !args[1].IsNumber())
            return Error(vm, "randomInt", "expected (min: integer, max: integer)");
        double loD = args[0].AsNumber();
        double hiD = args[1].AsNumber();
        if (loD != std::floor(loD) || hiD != std::floor(hiD) || loD > hiD || hiD - loD > 2147483647.0)
            return Error(vm, "randomInt", "min/max must be integers with max - min <= 2^31 - 1");
        const uint64_t lo = static_cast<uint64_t>(loD);
        const uint64_t range = static_cast<uint64_t>(hiD) - lo + 1;
        const uint32_t limit = static_cast<uint32_t>(4294967296ULL / range * range); // largest exact multiple
        for (;;) {
            auto r = RandomBytes(4);
            if (!r.ok) return Error(vm, "randomInt", r.error.message);
            uint32_t u = 0;
            for (int i = 0; i < 4; ++i) u = (u << 8) | r.value[i];
            if (u < limit)
                return Lode::Value(static_cast<double>(lo + (u % range)));
        }
    });

    exports.Function("pemEncode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsString()) return Error(vm, "pemEncode", "expected label and binary DER input");
        Input der; if (!RequireBytes(vm, args, 1, "pemEncode", der)) return {};
        auto result = PemEncode(args[0].AsStringView(), der.bytes); if (!result.ok) return Error(vm, "pemEncode", result.error.message); return BytesValue(vm, result.value);
    });
    exports.Function("pemDecode", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[1].IsString()) return Error(vm, "pemDecode", "expected PEM input and label");
        Input pem; if (!RequireBytes(vm, args, 0, "pemDecode", pem)) return {};
        auto result = PemDecode(pem.bytes, args[1].AsStringView()); if (!result.ok) return Error(vm, "pemDecode", result.error.message); return BytesValue(vm, result.value);
    });
    exports.Function("timingSafeEqual", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        Input left,right; if(!RequireBytes(vm,args,0,"timingSafeEqual",left)||!RequireBytes(vm,args,1,"timingSafeEqual",right))return {};
        return Lode::Value(ConstantTimeEqual(left.bytes,right.bytes));
    });
    exports.Function("hash", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input data; if(!RequireAlgorithm(vm,args,0,"hash",algorithm)||!RequireBytes(vm,args,1,"hash",data))return {};
        auto result=Digest(algorithm,data.bytes); if(!result.ok)return Error(vm,"hash",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("hmac", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,data; if(!RequireAlgorithm(vm,args,0,"hmac",algorithm)||!RequireBytes(vm,args,1,"hmac",key)||!RequireBytes(vm,args,2,"hmac",data))return {};
        auto result=Hmac(algorithm,key.bytes,data.bytes); if(!result.ok)return Error(vm,"hmac",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("hkdf", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input ikm,salt,info; uint64_t length=0; if(!RequireAlgorithm(vm,args,0,"hkdf",algorithm)||!RequireBytes(vm,args,1,"hkdf",ikm)||args.Size()<3||!ReadInteger(args[2].ToValue(),kMaxOutputBytes,length)){return Error(vm,"hkdf","length must be a finite non-negative integer within limit");}
        if(args.Size()>3&&!args[3].IsNil()&&!RequireBytes(vm,args,3,"hkdf",salt))return {}; if(args.Size()>4&&!args[4].IsNil()&&!RequireBytes(vm,args,4,"hkdf",info))return {};
        auto result=Hkdf(algorithm,ikm.bytes,static_cast<size_t>(length),salt.bytes,info.bytes);if(!result.ok)return Error(vm,"hkdf",result.error.message);return BytesValue(vm,result.value);
    });
    exports.Function("pbkdf2", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input password,salt; uint64_t iterations=0,length=0; if(!RequireAlgorithm(vm,args,0,"pbkdf2",algorithm)||!RequireBytes(vm,args,1,"pbkdf2",password)||!RequireBytes(vm,args,2,"pbkdf2",salt)||args.Size()<4||!ReadInteger(args[3].ToValue(),kMaxPbkdf2Iterations,iterations)||iterations==0||args.Size()<5||!ReadInteger(args[4].ToValue(),kMaxOutputBytes,length))return Error(vm,"pbkdf2","invalid iteration count or output length");
        auto result=Pbkdf2(algorithm,password.bytes,salt.bytes,static_cast<uint32_t>(iterations),static_cast<size_t>(length));if(!result.ok)return Error(vm,"pbkdf2",result.error.message);return BytesValue(vm,result.value);
    });
    exports.Function("aeadEncrypt", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,nonce,plaintext,aad; if(!RequireAlgorithm(vm,args,0,"aeadEncrypt",algorithm)||!RequireBytes(vm,args,1,"aeadEncrypt",key)||!RequireBytes(vm,args,2,"aeadEncrypt",nonce)||!RequireBytes(vm,args,3,"aeadEncrypt",plaintext))return {};
        if(args.Size()>4&&!args[4].IsNil()&&!RequireBytes(vm,args,4,"aeadEncrypt",aad))return {}; auto result=AeadEncrypt(algorithm,key.bytes,nonce.bytes,plaintext.bytes,aad.bytes);if(!result.ok)return Error(vm,"aeadEncrypt",result.error.message);return BytesValue(vm,result.value);
    });
    exports.Function("aeadDecrypt", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,nonce,ciphertext,aad; if(!RequireAlgorithm(vm,args,0,"aeadDecrypt",algorithm)||!RequireBytes(vm,args,1,"aeadDecrypt",key)||!RequireBytes(vm,args,2,"aeadDecrypt",nonce)||!RequireBytes(vm,args,3,"aeadDecrypt",ciphertext))return {};
        if(args.Size()>4&&!args[4].IsNil()&&!RequireBytes(vm,args,4,"aeadDecrypt",aad))return {}; auto result=AeadDecrypt(algorithm,key.bytes,nonce.bytes,ciphertext.bytes,aad.bytes);if(!result.ok)return Error(vm,"aeadDecrypt",result.error.authenticationFailure?"authentication failed":result.error.message);return BytesValue(vm,result.value);
    });

    exports.Function("generateKeyPair", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; if (!RequireAlgorithm(vm, args, 0, "generateKeyPair", algorithm)) return {};
        auto result = GenerateKeyPair(algorithm); if (!result.ok) return Error(vm, "generateKeyPair", result.error.message);
        Lode::Table output = vm.CreateTable(); output.Set("algorithm", result.value.algorithm); output.Set("privateKey", BytesValue(vm, result.value.privateKey)); output.Set("publicKey", BytesValue(vm, result.value.publicKey)); return Lode::Value(output);
    });
    exports.Function("sign", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key, data; if (!RequireAlgorithm(vm,args,0,"sign",algorithm)||!RequireBytes(vm,args,1,"sign",key)||!RequireBytes(vm,args,2,"sign",data)) return {};
        auto result=Sign(algorithm,key.bytes,data.bytes); if(!result.ok)return Error(vm,"sign",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("verify", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,data,signature; if(!RequireAlgorithm(vm,args,0,"verify",algorithm)||!RequireBytes(vm,args,1,"verify",key)||!RequireBytes(vm,args,2,"verify",data)||!RequireBytes(vm,args,3,"verify",signature))return {};
        auto result=Verify(algorithm,key.bytes,data.bytes,signature.bytes); if(!result.ok)return Error(vm,"verify",result.error.message); return Lode::Value(result.value);
    });
    exports.Function("encrypt", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,data; if(!RequireAlgorithm(vm,args,0,"encrypt",algorithm)||!RequireBytes(vm,args,1,"encrypt",key)||!RequireBytes(vm,args,2,"encrypt",data))return {};
        auto result=Encrypt(algorithm,key.bytes,data.bytes); if(!result.ok)return Error(vm,"encrypt",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("decrypt", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,data; if(!RequireAlgorithm(vm,args,0,"decrypt",algorithm)||!RequireBytes(vm,args,1,"decrypt",key)||!RequireBytes(vm,args,2,"decrypt",data))return {};
        auto result=Decrypt(algorithm,key.bytes,data.bytes); if(!result.ok)return Error(vm,"decrypt",result.error.message); return BytesValue(vm,result.value);
    });
    exports.Function("derive", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        std::string algorithm; Input key,peer; if(!RequireAlgorithm(vm,args,0,"derive",algorithm)||!RequireBytes(vm,args,1,"derive",key)||!RequireBytes(vm,args,2,"derive",peer))return {};
        auto result=Derive(algorithm,key.bytes,peer.bytes); if(!result.ok)return Error(vm,"derive",result.error.message); return BytesValue(vm,result.value);
    });

    return Lode::ModuleReturn(exports.GetExportTable());
}
