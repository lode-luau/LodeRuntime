// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define SECURITY_WIN32

#include "Http/HttpTls.hpp"
#include <cstring>
#include <algorithm>
#include <cstdio>

// Link Secur32 for SChannel/SSPI (CMake also adds it via target_link_libraries)
#pragma comment(lib, "Secur32.lib")

namespace lodehttp
{

TlsContext::TlsContext()
{
    SecInvalidateHandle(&cred_);
    SecInvalidateHandle(&ctx_);
}

TlsContext::~TlsContext()
{
    Shutdown();
}

bool TlsContext::Init()
{
    SCHANNEL_CRED cred{};
    cred.dwVersion             = SCHANNEL_CRED_VERSION;
    cred.grbitEnabledProtocols = 0; // Let the OS pick TLS 1.2/1.3
    cred.dwFlags               = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_AUTO_CRED_VALIDATION;

    SECURITY_STATUS ss = AcquireCredentialsHandleA(
        nullptr,
        const_cast<char*>(UNISP_NAME_A),
        SECPKG_CRED_OUTBOUND,
        nullptr, &cred,
        nullptr, nullptr,
        &cred_, nullptr);

    if (ss != SEC_E_OK)
        return false;
    credValid_ = true;
    return true;
}

std::vector<uint8_t> TlsContext::StartHandshake(const std::string& hostname)
{
    hostname_ = hostname;

    SecBuffer outBuf{};
    outBuf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, &outBuf };

    ULONG attribs = 0;
    InitializeSecurityContextA(
        &cred_, nullptr,
        const_cast<char*>(hostname_.c_str()),
        kContextReq, 0, 0,
        nullptr, 0,
        &ctx_, &outDesc,
        &attribs, nullptr);
    ctxValid_ = true;

    std::vector<uint8_t> result;
    if (outBuf.pvBuffer && outBuf.cbBuffer > 0)
    {
        result.assign(
            static_cast<uint8_t*>(outBuf.pvBuffer),
            static_cast<uint8_t*>(outBuf.pvBuffer) + outBuf.cbBuffer);
        FreeContextBuffer(outBuf.pvBuffer);
    }
    return result;
}

TlsContext::HandshakeResult TlsContext::ProcessHandshakeData(
    const uint8_t* data, size_t len,
    std::vector<uint8_t>& outSend,
    std::string& errorOut)
{
    pending_.insert(pending_.end(), data, data + len);

    SecBuffer inBufs[2] = {};
    inBufs[0].BufferType = SECBUFFER_TOKEN;
    inBufs[0].pvBuffer   = pending_.data();
    inBufs[0].cbBuffer   = static_cast<ULONG>(pending_.size());
    inBufs[1].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc inDesc{ SECBUFFER_VERSION, 2, inBufs };

    SecBuffer outBuf{};
    outBuf.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc outDesc{ SECBUFFER_VERSION, 1, &outBuf };

    ULONG attribs = 0;
    SECURITY_STATUS ss = InitializeSecurityContextA(
        &cred_, &ctx_,
        const_cast<char*>(hostname_.c_str()),
        kContextReq, 0, 0,
        &inDesc, 0,
        nullptr, &outDesc,
        &attribs, nullptr);

    // Collect output bytes to send to server
    if (outBuf.pvBuffer && outBuf.cbBuffer > 0)
    {
        outSend.assign(
            static_cast<uint8_t*>(outBuf.pvBuffer),
            static_cast<uint8_t*>(outBuf.pvBuffer) + outBuf.cbBuffer);
        FreeContextBuffer(outBuf.pvBuffer);
    }

    // Update pending_ with leftover bytes (extra bytes after the consumed token)
    if (inBufs[1].BufferType == SECBUFFER_EXTRA && inBufs[1].cbBuffer > 0)
    {
        size_t extraStart = pending_.size() - inBufs[1].cbBuffer;
        std::vector<uint8_t> extra(pending_.begin() + extraStart, pending_.end());
        pending_ = std::move(extra);
    }
    else
    {
        pending_.clear();
    }

    if (ss == SEC_E_INCOMPLETE_MESSAGE)
        return HandshakeResult::NeedMoreData;

    if (ss == SEC_I_CONTINUE_NEEDED || ss == SEC_I_INCOMPLETE_CREDENTIALS)
        return outSend.empty() ? HandshakeResult::NeedMoreData : HandshakeResult::DataToSend;

    if (ss == SEC_E_OK)
    {
        handshakeDone_ = true;
        QueryContextAttributes(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
        return HandshakeResult::Complete;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(ss));
    errorOut = std::string("TLS handshake error: ") + buf;
    return HandshakeResult::Error;
}

std::vector<uint8_t> TlsContext::DrainPending()
{
    if (pending_.empty())
        return {};
    std::string err;
    return Decrypt(nullptr, 0, err);
}

std::vector<uint8_t> TlsContext::Encrypt(const void* data, size_t len, std::string& errorOut)
{
    const auto* src = static_cast<const uint8_t*>(data);
    std::vector<uint8_t> result;
    size_t offset = 0;

    while (offset < len)
    {
        size_t chunk = std::min(len - offset, static_cast<size_t>(sizes_.cbMaximumMessage));
        size_t total = sizes_.cbHeader + chunk + sizes_.cbTrailer;

        std::vector<uint8_t> buf(total);
        std::memcpy(buf.data() + sizes_.cbHeader, src + offset, chunk);

        SecBuffer encBufs[4] = {};
        encBufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        encBufs[0].pvBuffer   = buf.data();
        encBufs[0].cbBuffer   = sizes_.cbHeader;
        encBufs[1].BufferType = SECBUFFER_DATA;
        encBufs[1].pvBuffer   = buf.data() + sizes_.cbHeader;
        encBufs[1].cbBuffer   = static_cast<ULONG>(chunk);
        encBufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        encBufs[2].pvBuffer   = buf.data() + sizes_.cbHeader + chunk;
        encBufs[2].cbBuffer   = sizes_.cbTrailer;
        encBufs[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc{ SECBUFFER_VERSION, 4, encBufs };

        SECURITY_STATUS ss = EncryptMessage(&ctx_, 0, &desc, 0);
        if (ss != SEC_E_OK)
        {
            char ebuf[64];
            snprintf(ebuf, sizeof(ebuf), "0x%08lX", static_cast<unsigned long>(ss));
            errorOut = std::string("TLS encrypt error: ") + ebuf;
            return {};
        }

        size_t written = encBufs[0].cbBuffer + encBufs[1].cbBuffer + encBufs[2].cbBuffer;
        result.insert(result.end(), buf.begin(), buf.begin() + written);
        offset += chunk;
    }
    return result;
}

std::vector<uint8_t> TlsContext::Decrypt(const uint8_t* data, size_t len, std::string& errorOut)
{
    if (data && len > 0)
        pending_.insert(pending_.end(), data, data + len);

    std::vector<uint8_t> result;

    while (!pending_.empty())
    {
        SecBuffer decBufs[4] = {};
        decBufs[0].BufferType = SECBUFFER_DATA;
        decBufs[0].pvBuffer   = pending_.data();
        decBufs[0].cbBuffer   = static_cast<ULONG>(pending_.size());
        decBufs[1].BufferType = SECBUFFER_EMPTY;
        decBufs[2].BufferType = SECBUFFER_EMPTY;
        decBufs[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc desc{ SECBUFFER_VERSION, 4, decBufs };

        SECURITY_STATUS ss = DecryptMessage(&ctx_, &desc, 0, nullptr);

        if (ss == SEC_E_INCOMPLETE_MESSAGE)
            break; // Need more data; pending_ already holds the partial record.

        if (ss == SEC_I_CONTEXT_EXPIRED || ss == SEC_I_RENEGOTIATE)
        {
            // Remote sent a close_notify or requests renegotiation — treat as EOF.
            pending_.clear();
            break;
        }

        if (ss != SEC_E_OK)
        {
            char ebuf[64];
            snprintf(ebuf, sizeof(ebuf), "0x%08lX", static_cast<unsigned long>(ss));
            errorOut = std::string("TLS decrypt error: ") + ebuf;
            pending_.clear();
            return result;
        }

        // Collect plaintext and any leftover (SECBUFFER_EXTRA) for the next iteration.
        std::vector<uint8_t> extra;
        for (int i = 0; i < 4; ++i)
        {
            if (decBufs[i].BufferType == SECBUFFER_DATA && decBufs[i].cbBuffer > 0)
            {
                const auto* p = static_cast<const uint8_t*>(decBufs[i].pvBuffer);
                result.insert(result.end(), p, p + decBufs[i].cbBuffer);
            }
            else if (decBufs[i].BufferType == SECBUFFER_EXTRA && decBufs[i].cbBuffer > 0)
            {
                const auto* p = static_cast<const uint8_t*>(decBufs[i].pvBuffer);
                extra.assign(p, p + decBufs[i].cbBuffer);
            }
        }
        pending_ = std::move(extra);
    }

    return result;
}

void TlsContext::Shutdown()
{
    if (ctxValid_)
    {
        DeleteSecurityContext(&ctx_);
        ctxValid_ = false;
    }
    if (credValid_)
    {
        FreeCredentialsHandle(&cred_);
        credValid_ = false;
    }
    handshakeDone_ = false;
    pending_.clear();
}

} // namespace lodehttp

#endif // _WIN32
