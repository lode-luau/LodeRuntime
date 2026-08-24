--!strict
--!native

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

--- File status information
export type FileStat = {
    --- The size of the file in bytes
    size: number,
    --- Whether the path is a directory
    isDirectory: boolean,
    --- Whether the path is a regular file
    isFile: boolean,
    --- Whether the path is a symbolic link
    isSymbolicLink: boolean,
    --- Target path for a symbolic link
    linkTarget: string?,
    --- Permission bits represented as a numeric mode
    mode: number,
    --- Last modification time (unix timestamp in seconds)
    mtime: number,
    --- Last access time (unix timestamp in seconds)
    atime: number,
    --- Status change time (unix timestamp in seconds)
    ctime: number
}

--- Handle to an open file
export type FileHandle = {
    --- Reads from the file returning a string
    Read: (self: FileHandle, length: number, offset: number?) -> string,
    --- Reads from the file returning a buffer
    ReadBuffer: (self: FileHandle, length: number, offset: number?) -> buffer,
    --- Writes to the file
    Write: (self: FileHandle, data: string | buffer, offset: number?) -> (),
    --- Repositions the file cursor and returns its absolute byte position.
    --- The whence value is "set" (default), "cur", or "end".
    --- FileHandle operations must be used sequentially; append-mode handles cannot seek.
    Seek: (self: FileHandle, offset: number, whence: ("set" | "cur" | "end")?) -> number,
    --- Retrieves file status for the handle
    Stat: (self: FileHandle) -> FileStat,
    --- Synchronizes file state to disk
    Sync: (self: FileHandle) -> (),
    --- Closes the file
    Close: (self: FileHandle) -> (),
    --- Returns whether the file is open
    IsOpen: (self: FileHandle) -> boolean
}

--- Readable stream for a file
export type ReadStream = {
    --- Signal emitted when data is received (buffer)
    DataReceived: Signal<buffer>,
    --- Signal emitted when the stream reaches EOF
    EndOfStream: Signal<>,
    --- Signal emitted on error
    ErrorOccurred: Signal<string>,
    --- Starts the stream
    Start: (self: ReadStream) -> (),
    --- Closes the stream
    Close: (self: ReadStream) -> ()
}

--- Watcher for file system changes
export type FileWatcher = {
    --- Signal emitted when the file changes
    Changed: Signal<string, string>,
    --- Signal emitted on error
    ErrorOccurred: Signal<string>,
    --- Starts the watcher (recursive optionally)
    Start: (self: FileWatcher, recursive: boolean?) -> (),
    --- Stops the watcher
    Stop: (self: FileWatcher) -> ()
}

--[[
    Filesystem Module (fs).
    Provides functions for interacting with the file system.
    Operations are executed asynchronously in libuv's thread pool.
    
    Usage:
    ```lua
    local fs = require("@filesystem")
    
    fs.WriteFile("test.txt", "Hello File!")
    local contents = fs.ReadFile("test.txt")
    print(contents)
    
    local files = fs.ReadDirectory(".")
    for _, file in ipairs(files) do
        print(file)
    end
    ```
]]
export type FileSystemModule = {
    -- =======================================================
    -- Coroutine-Yielding Methods
    -- Suspends current coroutine and resumes when ready.
    -- On error, throws a lua error catchable via pcall.
    -- =======================================================

    --- Reads a file asynchronously and returns its contents as a string.
    --- @return The file contents
    ReadFile: (
        --- The path to the file
        filePath: string
    ) -> string,

    --- Reads a file asynchronously and returns its contents as a buffer.
    --- @return The file contents as a buffer
    ReadFileBuffer: (
        --- The path to the file
        filePath: string
    ) -> buffer,

    --- Reads a file asynchronously into a provided buffer.
    --- @return The number of bytes read
    ReadToBuffer: (
        --- The path to the file
        filePath: string,
        --- The buffer to read into
        buf: buffer,
        --- Optional byte offset in the buffer (default 0)
        offset: number?
    ) -> number,

    --- Writes a string or buffer to a file asynchronously.
    WriteFile: (
        --- The path to the file
        filePath: string,
        --- The data to write
        data: string | buffer
    ) -> (),

    --- Appends a string or buffer to a file asynchronously.
    AppendFile: (
        --- The path to the file
        filePath: string,
        --- The data to append
        data: string | buffer
    ) -> (),

    --- Copies a file or directory tree asynchronously.
    CopyFile: (
        --- The source file path
        srcPath: string,
        --- The destination file path
        destPath: string
    ) -> (),

    --- Renames or moves a file asynchronously.
    Rename: (
        --- The current file path
        oldPath: string,
        --- The new file path
        newPath: string
    ) -> (),

    --- Checks if a file or directory exists asynchronously.
    --- @return True if it exists, false otherwise
    Exists: (
        --- The path to check
        path: string
    ) -> boolean,

    --- Creates a directory and all parent directories asynchronously.
    CreateDirectory: (
        --- The directory path to create
        dirPath: string
    ) -> (),

    --- Reads the contents of a directory asynchronously.
    --- @return An array of file and folder names
    ReadDirectory: (
        --- The directory path
        dirPath: string
    ) -> { string },

    --- Finds files matching *, ?, and ** glob patterns asynchronously.
    --- @return Paths relative to the non-wildcard portion of the pattern
    Glob: (
        --- The glob pattern
        pattern: string
    ) -> { string },

    --- Retrieves file status information asynchronously.
    --- @return A table with file statistics
    Stat: (
        --- The path to check
        path: string
    ) -> FileStat,

    --- Resolves the absolute path of a file asynchronously.
    --- @return The real absolute path
    RealPath: (
        --- The path to resolve
        path: string
    ) -> string,

    --- Creates a symbolic link asynchronously.
    CreateSymlink: (targetPath: string, linkPath: string) -> (),

    --- Reads the target of a symbolic link asynchronously.
    ReadLink: (linkPath: string) -> string,

    --- Replaces permission bits using a numeric mode such as 0o644.
    SetPermissions: (path: string, mode: number) -> (),
    setPermissions: (path: string, mode: number) -> (),
    Chmod: (path: string, mode: number) -> (),
    chmod: (path: string, mode: number) -> (),

    --- Removes a file asynchronously.
    RemoveFile: (
        --- The path to the file to remove
        filePath: string
    ) -> (),

    --- Removes a directory asynchronously.
    RemoveDirectory: (
        --- The path to the directory to remove
        dirPath: string,
        --- Whether to remove recursively (default false)
        recursive: boolean?
    ) -> (),

    -- =======================================================
    -- Callbacks and Objects
    -- =======================================================
    
    --- File interactions using streams and handles
    File: {
        --- Opens a file and returns a handle.
        --- Modes: "r", "w", "a", "r+", "w+", "a+" plus the exclusive
        --- variants "x", "wx", "ax" (fail when the file already exists).
        --- @return A FileHandle instance
        Open: (self: any, path: string, mode: string?) -> FileHandle
    },
    
    --- Readable stream using signals
    ReadStream: {
        --- Creates a ReadStream for a file
        --- @return A ReadStream instance
        Create: (self: any, path: string) -> ReadStream
    },
    
    --- File system watcher
    Watcher: {
        --- Creates a Watcher for a file or directory
        --- @return A FileWatcher instance
        Create: (self: any, path: string) -> FileWatcher
    }
}

return {} :: FileSystemModule
