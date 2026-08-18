--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- Represents a connection to a signal.
export type SignalConnection = {
    --- Whether this connection is still active.
    Connected: boolean,
    --- Disconnects this connection, preventing the callback from being fired.
    --- @param self The connection instance
    Disconnect: (self: SignalConnection) -> (),
}

--- A native signal implementation for events.
export type Signal<T...> = {
    --- Connects a callback to the signal.
    --- @return A connection object that can be disconnected
    Connect: (
        self: Signal<T...>,
        --- The function to call when the signal fires
        callback: (T...) -> ()
    ) -> SignalConnection,

    --- Creates a connection that will only fire once.
    --- @return A connection object that can be disconnected
    Once: (
        self: Signal<T...>,
        --- The function to call when the signal fires
        callback: (T...) -> ()
    ) -> SignalConnection,

    --- Yields the current thread until the signal fires.
    --- @return The arguments passed to Fire
    Wait: (
        self: Signal<T...>,
        --- Optional max seconds to wait
        timeout: number?
    ) -> T...,
}

--- HTTP request methods.
export type HttpMethod = "GET" | "POST" | "PUT" | "DELETE" | "PATCH" | "HEAD" | "OPTIONS" | "CONNECT" | "TRACE"

--- HTTP headers map.
export type HttpHeaders = { [string]: string }

--- Options for configuring an HTTP fetch.
export type FetchOptions = {
    --- The HTTP method to use
    method: HttpMethod?,
    --- Additional headers to send
    headers: HttpHeaders?,
    --- The body of the request
    body: string | buffer?,
    --- The timeout in milliseconds
    timeout: number?,
    --- The maximum number of redirects to follow
    maxRedirects: number?,
    --- Whether to upload the body using chunked transfer encoding
    chunked: boolean?,
    --- Alias for chunked
    chunkedUpload: boolean?,
    --- Callback invoked as response chunks/data arrive
    onData: ((chunk: string) -> ())?,
    --- Alias for onData
    onChunk: ((chunk: string) -> ())?,
    --- Whether a reusable HttpClient may retain the connection (default true)
    keepAlive: boolean?,
}

--- Options for the bounded HttpClient keep-alive pool.
export type ClientOptions = {
    --- Maximum number of idle connections retained by this client (default 4)
    maxIdleConnections: number?,
    --- Maximum requests served by one connection (default 100)
    maxConnectionUses: number?,
    --- Idle connection lifetime in milliseconds (default 30000)
    idleTimeout: number?,
}

--- Options for configuring an HTTP/HTTPS server.
export type ServerOptions = {
    --- Whether to enable TLS/HTTPS
    tls: boolean?,
    --- Alias for tls
    ssl: boolean?,
}

--- Represents an HTTP response body.
export type HttpBody = {
    --- Parses the response body as JSON and returns a table.
    AsTable: (self: HttpBody) -> { [string]: any },
    
    --- Formats the response body into a valid JSON string.
    AsJson: (self: HttpBody, pretty: boolean?) -> string,
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
    --- The response body
    body: HttpBody,
}

--- Represents an HTTP server.
export type HttpServer = {
    --- Fired when a request is received. Handlers connect to this signal to route/respond.
    RequestReceived: Signal<any, any>,
    --- Fired on underlying server errors.
    ErrorOccurred: Signal<string>,

    --- Listens on the specified port.
    Listen: (
        self: HttpServer,
        --- The port to listen on
        port: number,
        --- Optional host to bind to
        host: string?,
        --- Optional server options
        opts: ServerOptions?
    ) -> (),
    
    --- Closes the server.
    Close: (self: HttpServer) -> (),
    
    --- Closes the server and destroys native handles.
    Destroy: (self: HttpServer) -> (),
    
    --- Returns whether the server is listening.
    IsListening: (self: HttpServer) -> boolean,
    
    --- Returns the local address.
    LocalAddress: (self: HttpServer) -> { host: string, port: number },
}

--- Options for configuring a client request.
export type RequestOptions = FetchOptions

--- Represents an HTTP client for reusing connections.
export type HttpClient = {
    --- Fired when a response is received asynchronously.
    ResponseReceived: Signal<HttpResponse>,
    --- Fired when an error occurs during an asynchronous request.
    ErrorOccurred: Signal<string>,

    --- Sends a request and yields the coroutine until the response returns.
    Request: (
        self: HttpClient,
        --- The URL
        url: string,
        --- Options
        opts: RequestOptions?
    ) -> HttpResponse,
    
    --- Sends a request asynchronously. Does NOT yield.
    RequestAsync: (
        self: HttpClient,
        --- The URL
        url: string,
        --- Options
        opts: RequestOptions?
    ) -> (),

    --- Gracefully closes the client and its persistent connections.
    Close: (self: HttpClient) -> (),
}

--- The main HTTP module interface.
export type HttpModule = {
    --- Simple, coroutine-yielding request. 
    --- Yields the current thread until the response is fully received or an error occurs.
    fetch: (
        --- The target URL
        url: string,
        --- Request configuration
        opts: FetchOptions?
    ) -> HttpResponse,
    
    --- The Server namespace
    Server: {
        --- Creates a new HTTP Server
        Create: (self: any, opts: ServerOptions?) -> HttpServer
    },
    
    --- The Client namespace
    HttpClient: {
        --- Creates a new HTTP Client
        Create: (self: any, opts: ClientOptions?) -> HttpClient
    },
    
    --- The Status Codes enum
    StatusCodes: {
        --- 100 Continue
        Continue: number,
        --- 101 Switching Protocols
        SwitchingProtocols: number,
        --- 102 Processing
        Processing: number,
        
        --- 200 OK
        Ok: number,
        --- 201 Created
        Created: number,
        --- 202 Accepted
        Accepted: number,
        --- 203 Non-Authoritative Information
        NonAuthoritativeInformation: number,
        --- 204 No Content
        NoContent: number,
        --- 205 Reset Content
        ResetContent: number,
        --- 206 Partial Content
        PartialContent: number,
        --- 207 Multi-Status
        MultiStatus: number,
        --- 208 Already Reported
        AlreadyReported: number,
        --- 226 IM Used
        IMUsed: number,
        
        --- 300 Multiple Choices
        MultipleChoices: number,
        --- 301 Moved Permanently
        MovedPermanently: number,
        --- 302 Found
        Found: number,
        --- 303 See Other
        SeeOther: number,
        --- 304 Not Modified
        NotModified: number,
        --- 305 Use Proxy
        UseProxy: number,
        --- 307 Temporary Redirect
        TemporaryRedirect: number,
        --- 308 Permanent Redirect
        PermanentRedirect: number,
        
        --- 400 Bad Request
        BadRequest: number,
        --- 401 Unauthorized
        Unauthorized: number,
        --- 402 Payment Required
        PaymentRequired: number,
        --- 403 Forbidden
        Forbidden: number,
        --- 404 Not Found
        NotFound: number,
        --- 405 Method Not Allowed
        MethodNotAllowed: number,
        --- 406 Not Acceptable
        NotAcceptable: number,
        --- 407 Proxy Authentication Required
        ProxyAuthenticationRequired: number,
        --- 408 Request Timeout
        RequestTimeout: number,
        --- 409 Conflict
        Conflict: number,
        --- 410 Gone
        Gone: number,
        --- 411 Length Required
        LengthRequired: number,
        --- 412 Precondition Failed
        PreconditionFailed: number,
        --- 413 Payload Too Large
        PayloadTooLarge: number,
        --- 414 URI Too Long
        UriTooLong: number,
        --- 415 Unsupported Media Type
        UnsupportedMediaType: number,
        --- 416 Range Not Satisfiable
        RangeNotSatisfiable: number,
        --- 417 Expectation Failed
        ExpectationFailed: number,
        --- 418 I'm a teapot
        ImATeapot: number,
        --- 421 Misdirected Request
        MisdirectedRequest: number,
        --- 422 Unprocessable Entity
        UnprocessableEntity: number,
        --- 423 Locked
        Locked: number,
        --- 424 Failed Dependency
        FailedDependency: number,
        --- 426 Upgrade Required
        UpgradeRequired: number,
        --- 428 Precondition Required
        PreconditionRequired: number,
        --- 429 Too Many Requests
        TooManyRequests: number,
        --- 431 Request Header Fields Too Large
        RequestHeaderFieldsTooLarge: number,
        --- 451 Unavailable For Legal Reasons
        UnavailableForLegalReasons: number,
        
        --- 500 Internal Server Error
        InternalServerError: number,
        --- 501 Not Implemented
        NotImplemented: number,
        --- 502 Bad Gateway
        BadGateway: number,
        --- 503 Service Unavailable
        ServiceUnavailable: number,
        --- 504 Gateway Timeout
        GatewayTimeout: number,
        --- 505 HTTP Version Not Supported
        HttpVersionNotSupported: number,
        --- 506 Variant Also Negotiates
        VariantAlsoNegotiates: number,
        --- 507 Insufficient Storage
        InsufficientStorage: number,
        --- 508 Loop Detected
        LoopDetected: number,
        --- 510 Not Extended
        NotExtended: number,
        --- 511 Network Authentication Required
        NetworkAuthenticationRequired: number,
    },
}

return {} :: HttpModule
