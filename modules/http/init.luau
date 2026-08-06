--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- @module http
--- Native HTTP/1.1 client module. All network I/O is asynchronous and runs on
--- the LibUV event loop. The body of a response is fully buffered by the
--- client, so `HttpBodyStream` reads resolve immediately.

export type HttpMethod = "GET" | "POST" | "PUT" | "DELETE" | "PATCH" | "HEAD" | "OPTIONS" | "CONNECT" | "TRACE"

export type HttpErrorKind =
    | "dns"
    | "connection-refused"
    | "timeout"
    | "aborted"
    | "invalid-url"
    | "malformed-response"
    | "redirects"
    | "tls"

export type HttpError = string

export type HttpRedirectPolicy = "follow" | "manual"

export type HttpHeaders = { [string]: string }

export type RequestOptions = {
    method: HttpMethod?,
    headers: HttpHeaders?,
    body: string | buffer?,
    timeout: number?,
    redirect: HttpRedirectPolicy?,
    maxRedirects: number?,
}

export type HttpBodyStream = {
    read: (self: HttpBodyStream, bytes: number?) -> string,
    readBuffer: (self: HttpBodyStream, bytes: number?) -> buffer,
    readLine: (self: HttpBodyStream) -> string?,
    readInto: (self: HttpBodyStream, buf: buffer, offset: number?, length: number?) -> number,
    readAsync: (self: HttpBodyStream, callback: (data: string) -> (), bytes: number?) -> (),
    readBufferAsync: (self: HttpBodyStream, callback: (data: buffer) -> (), bytes: number?) -> (),
    readIntoAsync: (self: HttpBodyStream, buf: buffer, callback: (bytesRead: number) -> (), offset: number?, length: number?) -> (),
}

export type HttpResponse = {
    status: number,
    statusText: string,
    version: string,
    url: string,
    headers: HttpHeaders,
    body: HttpBodyStream,
    text: (self: HttpResponse) -> string,
    json: <T>(self: HttpResponse) -> T,
}

export type HttpStatusCodes = { [string]: number }

export type HttpModule = {
    statusCode: HttpStatusCodes,
    request: (url: string, opts: RequestOptions?) -> HttpResponse,
    requestWithCallback: (url: string, opts: RequestOptions, callback: (response: HttpResponse?, error: HttpError?) -> ()) -> (),
    get: (url: string, opts: RequestOptions?) -> HttpResponse,
    getWithCallback: (url: string, opts: RequestOptions, callback: (response: HttpResponse?, error: HttpError?) -> ()) -> (),
    post: (url: string, body: string | buffer?, opts: RequestOptions?) -> HttpResponse,
    postWithCallback: (url: string, body: string | buffer?, opts: RequestOptions, callback: (response: HttpResponse?, error: HttpError?) -> ()) -> (),
    abort: () -> (),
}

return {} :: HttpModule
