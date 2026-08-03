--!strict
--!native

--[[
    Standard Input/Output Module (stdio).
    Provides streams for interacting with the console and OS standard file descriptors.
    Optimized for zero-copy buffer manipulation and fully asynchronous I/O via libuv.
]]

--- Interface for writable streams like standard output and standard error.
export type WritableStream = {
    --- Writes raw data (string or buffer) to the stream asynchronously.
    --- @param self The stream instance
    --- @param data The text or binary buffer to write
    write: (self: WritableStream, data: string | buffer) -> (),
    
    --- Writes data to the stream followed by a newline character.
    --- @param self The stream instance
    --- @param data The text to write
    writeLine: (self: WritableStream, data: string) -> (),
    
    --- Returns whether the stream is connected to a TTY (terminal).
    isTTY: (self: WritableStream) -> boolean,
    
    --- Gets the dimensions of the terminal.
    --- @return The width (columns) and height (rows) of the terminal. Returns 0, 0 if not a TTY.
    getWindowSize: (self: WritableStream) -> (number, number),
}

--- Interface for readable streams like standard input.
--- All read operations are fully asynchronous and non-blocking to the main thread.
export type ReadableStream = {
    -- =======================================================
    -- Coroutine-Yielding Methods (Appears Sync to Luau)
    -- =======================================================

    --- Reads a specified number of bytes, or the next available chunk.
    --- Yields the current coroutine until data is available.
    --- @param self The stream instance
    --- @param bytes Optional maximum number of bytes to read
    --- @return The data read, returned as a string
    read: (self: ReadableStream, bytes: number?) -> string,

    --- Reads data as a buffer instead of a string.
    --- Yields the current coroutine until data is available.
    --- @param self The stream instance
    --- @param bytes Optional maximum number of bytes to read
    --- @return The data read, returned as a buffer
    readBuffer: (self: ReadableStream, bytes: number?) -> buffer,
    
    --- Reads from the stream until a newline character is encountered.
    --- Yields the current coroutine until the line is complete.
    --- @param self The stream instance
    --- @return The line read, as a string
    readLine: (self: ReadableStream) -> string?,
    
    --- Reads data directly into an existing buffer, avoiding memory allocations.
    --- Yields the current coroutine until data is available.
    --- @param self The stream instance
    --- @param buf The target buffer to write into
    --- @param offset The offset in the buffer to start writing (defaults to 0)
    --- @param length The maximum number of bytes to read (defaults to remaining buffer space)
    --- @return The number of bytes actually read
    readInto: (self: ReadableStream, buf: buffer, offset: number?, length: number?) -> number,
    
    --- Returns whether the stream is connected to a TTY (terminal).
    isTTY: (self: ReadableStream) -> boolean,
    
    --- Enables or disables Raw Mode for the terminal.
    --- In Raw Mode, input is received immediately without waiting for the Enter key.
    --- @param enable True to enable raw mode, false to restore normal mode.
    setRawMode: (self: ReadableStream, enable: boolean) -> (),

    -- =======================================================
    -- Callback-Based Methods
    -- =======================================================

    --- Reads a specified number of bytes and fires a callback.
    --- @param self The stream instance
    --- @param callback Function called with the string data
    --- @param bytes Optional maximum number of bytes to read
    readAsync: (self: ReadableStream, callback: (data: string) -> (), bytes: number?) -> (),

    --- Reads data as a buffer and fires a callback.
    --- @param self The stream instance
    --- @param callback Function called with the buffer data
    --- @param bytes Optional maximum number of bytes to read
    readBufferAsync: (self: ReadableStream, callback: (data: buffer) -> (), bytes: number?) -> (),

    --- Reads data directly into an existing buffer and fires a callback.
    --- @param self The stream instance
    --- @param buf The target buffer
    --- @param callback Function called with the number of bytes read
    --- @param offset The optional offset in the buffer
    --- @param length Optional maximum bytes to read
    readIntoAsync: (self: ReadableStream, buf: buffer, callback: (bytesRead: number) -> (), offset: number?, length: number?) -> (),
}

--- The public interface for the Stdio module.
export type StdioModule = {
    --- Standard Input Stream (File descriptor 0)
    stdin: ReadableStream,
    
    --- Standard Output Stream (File descriptor 1)
    stdout: WritableStream,
    
    --- Standard Error Stream (File descriptor 2)
    stderr: WritableStream,
    
    --- Prints space-separated values to stdout, followed by a newline.
    print: (...any) -> (),
    
    --- Prints space-separated values to stderr, followed by a newline.
    eprint: (...any) -> (),
    
    --- Displays an optional prompt message and yields until the user inputs a line of text.
    --- @param message Optional text to display before waiting for input
    prompt: (message: string?) -> string,
    
    --- Shortcut to write directly to stdout without a newline.
    write: (data: string | buffer) -> (),
    
    --- Clears the console screen.
    clear: () -> (),
    
    --- Displays an interactive selection menu using the arrow keys.
    --- Requires a TTY terminal.
    --- @param prompt The question to ask the user.
    --- @param options An array of string options to choose from.
    --- @return The 1-based index of the chosen option, and the option string itself.
    select: (prompt: string, options: {string}) -> (number, string),
}

return {} :: StdioModule
