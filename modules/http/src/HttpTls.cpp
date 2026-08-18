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

#elif defined(LODE_HAS_OPENSSL_TLS)

#include "Http/HttpTls.hpp"
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <climits>

namespace lodehttp
{

struct TlsContext::Impl
{
    SSL_CTX* context = nullptr;
    SSL* session = nullptr;
};

static std::string LastTlsError(const char* operation)
{
    const unsigned long code = ERR_get_error();
    if (code == 0)
        return std::string("TLS ") + operation + " failed";

    char detail[256];
    ERR_error_string_n(code, detail, sizeof(detail));
    return std::string("TLS ") + operation + " failed: " + detail;
}

static std::vector<uint8_t> DrainOutput(SSL* session)
{
    std::vector<uint8_t> output;
    BIO* bio = SSL_get_wbio(session);
    uint8_t buffer[16 * 1024];
    while (BIO_ctrl_pending(bio) > 0)
    {
        const int count = BIO_read(bio, buffer, sizeof(buffer));
        if (count <= 0)
            break;
        output.insert(output.end(), buffer, buffer + count);
    }
    return output;
}

TlsContext::TlsContext() = default;

TlsContext::~TlsContext()
{
    Shutdown();
}

bool TlsContext::Init()
{
    Shutdown();
    impl_ = new Impl();
    impl_->context = SSL_CTX_new(TLS_client_method());
    if (!impl_->context)
    {
        Shutdown();
        return false;
    }

    SSL_CTX_set_verify(impl_->context, SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(impl_->context) != 1)
    {
        Shutdown();
        return false;
    }
    return true;
}

std::vector<uint8_t> TlsContext::StartHandshake(const std::string& hostname)
{
    if (!impl_ || !impl_->context)
        return {};

    impl_->session = SSL_new(impl_->context);
    BIO* input = BIO_new(BIO_s_mem());
    BIO* output = BIO_new(BIO_s_mem());
    if (!impl_->session || !input || !output)
    {
        if (input) BIO_free(input);
        if (output) BIO_free(output);
        return {};
    }

    SSL_set_bio(impl_->session, input, output);
    SSL_set_connect_state(impl_->session);
    if (SSL_set_tlsext_host_name(impl_->session, hostname.c_str()) != 1 ||
        SSL_set1_host(impl_->session, hostname.c_str()) != 1)
        return {};

    ERR_clear_error();
    const int result = SSL_do_handshake(impl_->session);
    if (result != 1 && SSL_get_error(impl_->session, result) != SSL_ERROR_WANT_READ)
        return {};
    return DrainOutput(impl_->session);
}

TlsContext::HandshakeResult TlsContext::ProcessHandshakeData(
    const uint8_t* data, size_t len, std::vector<uint8_t>& outSend,
    std::string& errorOut)
{
    if (!impl_ || !impl_->session)
    {
        errorOut = "TLS handshake was not initialized";
        return HandshakeResult::Error;
    }

    BIO* input = SSL_get_rbio(impl_->session);
    if (len > static_cast<size_t>(INT_MAX) || BIO_write(input, data, static_cast<int>(len)) != static_cast<int>(len))
    {
        errorOut = LastTlsError("input");
        return HandshakeResult::Error;
    }

    ERR_clear_error();
    const int result = SSL_do_handshake(impl_->session);
    outSend = DrainOutput(impl_->session);
    if (result == 1)
    {
        if (SSL_get_verify_result(impl_->session) != X509_V_OK)
        {
            errorOut = "TLS certificate verification failed";
            return HandshakeResult::Error;
        }
        handshakeDone_ = true;
        return HandshakeResult::Complete;
    }

    const int error = SSL_get_error(impl_->session, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
        return outSend.empty() ? HandshakeResult::NeedMoreData : HandshakeResult::DataToSend;

    errorOut = LastTlsError("handshake");
    return HandshakeResult::Error;
}

std::vector<uint8_t> TlsContext::DrainPending()
{
    std::string error;
    return Decrypt(nullptr, 0, error);
}

std::vector<uint8_t> TlsContext::Encrypt(const void* data, size_t len, std::string& errorOut)
{
    if (!impl_ || !impl_->session || !handshakeDone_)
    {
        errorOut = "TLS session is not ready";
        return {};
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    while (offset < len)
    {
        const int chunk = static_cast<int>(std::min(len - offset, static_cast<size_t>(INT_MAX)));
        ERR_clear_error();
        const int result = SSL_write(impl_->session, bytes + offset, chunk);
        if (result <= 0)
        {
            errorOut = LastTlsError("write");
            return {};
        }
        offset += static_cast<size_t>(result);
    }
    return DrainOutput(impl_->session);
}

std::vector<uint8_t> TlsContext::Decrypt(const uint8_t* data, size_t len, std::string& errorOut)
{
    std::vector<uint8_t> plaintext;
    if (!impl_ || !impl_->session)
    {
        errorOut = "TLS session is not ready";
        return plaintext;
    }

    BIO* input = SSL_get_rbio(impl_->session);
    if (data && len > 0)
    {
        if (len > static_cast<size_t>(INT_MAX) || BIO_write(input, data, static_cast<int>(len)) != static_cast<int>(len))
        {
            errorOut = LastTlsError("input");
            return plaintext;
        }
    }

    uint8_t buffer[16 * 1024];
    for (;;)
    {
        ERR_clear_error();
        const int result = SSL_read(impl_->session, buffer, sizeof(buffer));
        if (result > 0)
        {
            plaintext.insert(plaintext.end(), buffer, buffer + result);
            continue;
        }

        const int error = SSL_get_error(impl_->session, result);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_ZERO_RETURN)
            break;
        errorOut = LastTlsError("read");
        break;
    }
    return plaintext;
}

void TlsContext::Shutdown()
{
    handshakeDone_ = false;
    if (!impl_)
        return;
    if (impl_->session)
        SSL_free(impl_->session);
    if (impl_->context)
        SSL_CTX_free(impl_->context);
    delete impl_;
    impl_ = nullptr;
}

} // namespace lodehttp

#endif // _WIN32 / LODE_HAS_OPENSSL_TLS
