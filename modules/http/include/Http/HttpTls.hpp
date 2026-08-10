// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define SECURITY_WIN32
#include <windows.h>
#include <schannel.h>
#include <security.h>
#include <string>
#include <vector>
#include <cstdint>

namespace lodehttp
{

/**
 * @brief Wraps Windows SChannel/SSPI to provide client-side TLS for HTTPS requests.
 *
 * Lifecycle:
 *   1. Init()                  — acquire SSPI credentials (once)
 *   2. StartHandshake(host)    — returns ClientHello bytes to send
 *   3. ProcessHandshakeData()  — feed server bytes; returns status + optional reply bytes
 *      - repeat until Complete or Error
 *   4. Encrypt() / Decrypt()   — transport application data
 *   5. Shutdown()              — called automatically in destructor
 */
class TlsContext
{
public:
    TlsContext();
    ~TlsContext();

    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;

    /// Acquire SSPI client credentials. Returns false on failure.
    bool Init();

    /// Generate the initial ClientHello. Returns bytes to send to the server.
    std::vector<uint8_t> StartHandshake(const std::string& hostname);

    enum class HandshakeResult
    {
        NeedMoreData, ///< Feed more server bytes before calling again.
        DataToSend,   ///< Send outSend to server, then keep reading.
        Complete,     ///< Handshake finished; application data phase begins.
        Error,        ///< Fatal TLS error; errorOut contains description.
    };

    /**
     * @brief Feed received server bytes during the handshake.
     * @param data   Raw bytes received from the server.
     * @param len    Number of bytes.
     * @param outSend Filled with bytes to send back to the server (may be empty).
     * @param errorOut Error description on HandshakeResult::Error.
     */
    HandshakeResult ProcessHandshakeData(const uint8_t* data, size_t len,
                                         std::vector<uint8_t>& outSend,
                                         std::string& errorOut);

    /**
     * @brief Drain any pending bytes buffered inside the TLS context (e.g. early
     *        application data that arrived in the same TCP segment as the last
     *        handshake message). Returns decrypted bytes; usually empty.
     */
    std::vector<uint8_t> DrainPending();

    /**
     * @brief Encrypt plaintext application data into TLS records.
     * @param errorOut Error description on failure (returned vector will be empty).
     */
    std::vector<uint8_t> Encrypt(const void* data, size_t len, std::string& errorOut);

    /**
     * @brief Decrypt incoming TLS records into plaintext.
     *        Internally buffers incomplete records across calls.
     * @param errorOut Error description on failure.
     */
    std::vector<uint8_t> Decrypt(const uint8_t* data, size_t len, std::string& errorOut);

    bool IsHandshakeDone() const { return handshakeDone_; }

    void Shutdown();

private:
    CredHandle cred_{};
    CtxtHandle ctx_{};
    SecPkgContext_StreamSizes sizes_{};
    bool credValid_      = false;
    bool ctxValid_       = false;
    bool handshakeDone_  = false;
    std::string hostname_;
    std::vector<uint8_t> pending_; ///< Bytes received but not yet fully consumed.

    static constexpr ULONG kContextReq =
        ISC_REQ_SEQUENCE_DETECT  |
        ISC_REQ_REPLAY_DETECT    |
        ISC_REQ_CONFIDENTIALITY  |
        ISC_REQ_EXTENDED_ERROR   |
        ISC_REQ_ALLOCATE_MEMORY  |
        ISC_REQ_STREAM;
};

} // namespace lodehttp

#endif // _WIN32
