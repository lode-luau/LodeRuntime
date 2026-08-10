--!strict
--!native

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- HTTP request methods.
export type HttpMethod = "GET" | "POST" | "PUT" | "DELETE" | "PATCH" | "HEAD" | "OPTIONS" | "CONNECT" | "TRACE"

--- Kinds of HTTP errors that can occur.
export type HttpErrorKind =
    | "dns"
    | "connection-refused"
    | "timeout"
    | "aborted"
    | "invalid-url"
    | "malformed-response"
    | "redirects"
    | "tls"

--- An HTTP error message.
export type HttpError = string

--- HTTP redirect handling policies.
export type HttpRedirectPolicy = "follow" | "manual"

--- HTTP headers map.
export type HttpHeaders = { [string]: string }

--- Options for configuring an HTTP request.
export type RequestOptions = {
    --- The HTTP method to use
    method: HttpMethod?,
    --- Additional headers to send
    headers: HttpHeaders?,
    --- The body of the request
    body: string | buffer?,
    --- The timeout in milliseconds
    timeout: number?,
    --- How to handle redirects
    redirect: HttpRedirectPolicy?,
    --- The maximum number of redirects to follow
    maxRedirects: number?,
}

--- A stream interface for reading an HTTP response body.
export type HttpBodyStream = {
    --- Reads a specified number of bytes as a string.
    --- @return The read string
    read: (
        self: HttpBodyStream,
        --- Optional number of bytes to read
        bytes: number?
    ) -> string,
    
    --- Reads a specified number of bytes as a buffer.
    --- @return The read buffer
    readBuffer: (
        self: HttpBodyStream,
        --- Optional number of bytes to read
        bytes: number?
    ) -> buffer,
    
    --- Reads a single line as a string.
    --- @return The read line, or nil if EOF
    readLine: (self: HttpBodyStream) -> string?,
    
    --- Reads data directly into an existing buffer.
    --- @return The number of bytes read
    readInto: (
        self: HttpBodyStream,
        --- The buffer to read into
        buf: buffer,
        --- Optional offset to start writing at
        offset: number?,
        --- Optional maximum number of bytes to read
        length: number?
    ) -> number,
    
    --- Asynchronously reads a specified number of bytes as a string.
    readAsync: (
        self: HttpBodyStream,
        --- The function to call with the read data
        callback: (data: string) -> (),
        --- Optional number of bytes to read
        bytes: number?
    ) -> (),
    
    --- Asynchronously reads a specified number of bytes as a buffer.
    readBufferAsync: (
        self: HttpBodyStream,
        --- The function to call with the read data
        callback: (data: buffer) -> (),
        --- Optional number of bytes to read
        bytes: number?
    ) -> (),
    
    --- Asynchronously reads data directly into an existing buffer.
    readIntoAsync: (
        self: HttpBodyStream,
        --- The buffer to read into
        buf: buffer,
        --- The function to call with the number of bytes read
        callback: (bytesRead: number) -> (),
        --- Optional offset to start writing at
        offset: number?,
        --- Optional maximum number of bytes to read
        length: number?
    ) -> (),
}

--- Represents an HTTP response.
export type HttpResponse = {
    --- The HTTP status code
    status: number,
    --- The HTTP status text
    statusText: string,
    --- The HTTP protocol version
    version: string,
    --- The final URL of the response
    url: string,
    --- The response headers
    headers: HttpHeaders,
    --- The response body stream
    body: HttpBodyStream,
    
    --- Reads the entire response body as a string.
    --- @return The body text
    text: (self: HttpResponse) -> string,
    
    --- Reads the entire response body and parses it as JSON.
    --- @return The parsed JSON value
    json: <T>(self: HttpResponse) -> T,
}

--- HTTP status codes dictionary.
export type HttpStatusCodes = { [string]: number }

--[[
    Native HTTP/1.1 client module.
    Performs asynchronous HTTP requests. The response body is fully buffered.
    
    Usage:
    ```lua
    local http = require("@http")
    
    -- Synchronous yielding request
    local response = http.get("http://example.com")
    print(response.status, response:text())
    
    -- Asynchronous callback request
    http.getWithCallback("http://example.com", {}, function(res, err)
        if res then
            print(res:text())
        end
    end)
    ```
]]
export type HttpModule = {
    --- Common HTTP status codes mapping.
    statusCode: HttpStatusCodes,
    
    --- Performs an HTTP request and yields until completion.
    --- @return The HTTP response
    request: (
        --- The target URL
        url: string,
        --- Optional request options
        opts: RequestOptions?
    ) -> HttpResponse,
    
    --- Performs an HTTP request asynchronously and invokes a callback.
    requestWithCallback: (
        --- The target URL
        url: string,
        --- Request options
        opts: RequestOptions,
        --- The function to invoke with the response or error
        callback: (response: HttpResponse?, error: HttpError?) -> ()
    ) -> (),
    
    --- Performs an HTTP GET request and yields until completion.
    --- @return The HTTP response
    get: (
        --- The target URL
        url: string,
        --- Optional request options
        opts: RequestOptions?
    ) -> HttpResponse,
    
    --- Performs an HTTP GET request asynchronously and invokes a callback.
    getWithCallback: (
        --- The target URL
        url: string,
        --- Request options
        opts: RequestOptions,
        --- The function to invoke with the response or error
        callback: (response: HttpResponse?, error: HttpError?) -> ()
    ) -> (),
    
    --- Performs an HTTP POST request and yields until completion.
    --- @return The HTTP response
    post: (
        --- The target URL
        url: string,
        --- The request body
        body: string | buffer?,
        --- Optional request options
        opts: RequestOptions?
    ) -> HttpResponse,
    
    --- Performs an HTTP POST request asynchronously and invokes a callback.
    postWithCallback: (
        --- The target URL
        url: string,
        --- The request body
        body: string | buffer?,
        --- Request options
        opts: RequestOptions,
        --- The function to invoke with the response or error
        callback: (response: HttpResponse?, error: HttpError?) -> ()
    ) -> (),
    
    --- Aborts all pending HTTP requests.
    abort: () -> (),
}

return {} :: HttpModule
