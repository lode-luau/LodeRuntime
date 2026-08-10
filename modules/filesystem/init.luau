--!strict
--!native


export type PathModule = {
    --- Resolves paths into an absolute path.
    --- @return The resolved absolute path
    resolve: (
        --- The path segments to resolve
        ...string
    ) -> string,
    
    --- Joins path segments together.
    --- @return The joined path
    join: (
        --- The path segments to join
        ...string
    ) -> string,
    
    --- Returns the directory name of a path.
    --- @return The directory name
    dirname: (
        --- The path
        p: string
    ) -> string,
    
    --- Returns the base name of a path.
    --- @return The base name
    basename: (
        --- The path
        p: string
    ) -> string,
    
    --- Returns the extension of a path.
    --- @return The extension
    extname: (
        --- The path
        p: string
    ) -> string,
}

--- File status information
export type FileStat = {
    --- The size of the file in bytes
    size: number,
    --- Whether the path is a directory
    isDirectory: boolean,
    --- Whether the path is a regular file
    isFile: boolean,
    --- Last modification time (unix timestamp in seconds)
    mtime: number,
    --- Last access time (unix timestamp in seconds)
    atime: number,
    --- Status change time (unix timestamp in seconds)
    ctime: number
}

--[[
    Filesystem Module (fs).
    Provides functions for interacting with the file system.
    Operations are executed asynchronously in libuv's thread pool.
    
    Usage:
    ```lua
    local fs = require("@filesystem")
    
    fs.writeFile("test.txt", "Hello File!")
    local contents = fs.readFile("test.txt")
    print(contents)
    
    local files = fs.readDir(".")
    for _, file in ipairs(files) do
        print(file)
    end
    ```
]]
export type FileSystemModule = {
    --- Path utilities
    path: PathModule,

    -- =======================================================
    -- Coroutine-Yielding Methods
    -- Suspends current coroutine and resumes when ready.
    -- On error, throws a lua error catchable via pcall.
    -- =======================================================

    --- Reads a file asynchronously and returns its contents as a string.
    --- @return The file contents
    readFile: (
        --- The path to the file
        filePath: string
    ) -> string,

    --- Reads a file asynchronously and returns its contents as a buffer.
    --- @return The file contents as a buffer
    readFileBuffer: (
        --- The path to the file
        filePath: string
    ) -> buffer,

    --- Reads a file directly into a pre-allocated buffer with zero memory allocations.
    --- @return The number of bytes read
    readToBuffer: (
        --- The path to the file
        filePath: string,
        --- The buffer to write the data into
        buf: buffer,
        --- Optional offset in the buffer to start writing (defaults to 0)
        offset: number?
    ) -> number,

    --- Writes data to a file asynchronously, replacing its contents if it exists.
    writeFile: (
        --- The path to the file
        filePath: string,
        --- The data to write (string or buffer)
        contents: string | buffer
    ) -> (),

    --- Appends data to a file asynchronously, creating the file if it does not exist.
    appendFile: (
        --- The path to the file
        filePath: string,
        --- The data to append (string or buffer)
        contents: string | buffer
    ) -> (),

    --- Copies a file to a new location asynchronously. Overwrites the destination if it exists.
    copyFile: (
        --- The path to the source file
        srcPath: string,
        --- The path to the destination
        destPath: string
    ) -> (),

    --- Renames or moves a file or directory asynchronously.
    rename: (
        --- The current path
        oldPath: string,
        --- The new path
        newPath: string
    ) -> (),

    --- Checks if a path exists asynchronously.
    --- @return True if the path exists, false otherwise
    exists: (
        --- The path to check
        filePath: string
    ) -> boolean,

    --- Creates a directory and any necessary subdirectories asynchronously.
    mkdir: (
        --- The path to the directory
        filePath: string
    ) -> (),

    --- Reads the contents of a directory asynchronously.
    --- @return An array of strings representing the names of files and directories
    readDir: (
        --- The path to the directory
        dirPath: string
    ) -> { string },

    --- Retrieves file or directory information asynchronously.
    --- @return A FileStat table containing metadata
    stat: (
        --- The path to query
        filePath: string
    ) -> FileStat,

    --- Resolves a path to its absolute, canonical form, resolving symlinks asynchronously.
    --- @return The canonical path
    realpath: (
        --- The path to resolve
        filePath: string
    ) -> string,

    --- Removes a file or directory asynchronously.
    rm: (
        --- The path to remove
        filePath: string,
        --- Optional boolean to remove directories recursively
        recursive: boolean?
    ) -> (),

    -- =======================================================
    -- Callback-Based Methods
    -- Callback receives a boolean success indicator.
    -- If success is true, second arg is the data (if applicable).
    -- If success is false, second arg is the error message.
    -- =======================================================

    --- Reads a file asynchronously and executes a callback with its contents as a string.
    readFileWithCallback: (
        --- The path to the file
        filePath: string,
        --- Function called when complete. If success is true, data_or_err contains the string data. Otherwise, it contains the error message.
        callback: (success: boolean, data_or_err: string) -> ()
    ) -> (),
    
    --- Reads a file asynchronously and executes a callback with its contents as a buffer.
    readFileBufferWithCallback: (
        --- The path to the file
        filePath: string,
        --- Function called when complete. If success is true, data_or_err contains the buffer. Otherwise, it contains the error message.
        callback: (success: boolean, data_or_err: any) -> ()
    ) -> (),
    
    --- Reads a file directly into a pre-allocated buffer asynchronously and executes a callback.
    readToBufferWithCallback: (
        --- The path to the file
        filePath: string,
        --- The buffer to write the data into
        buf: buffer,
        --- Function called when complete. If success is true, bytes_or_err contains the number of bytes read. Otherwise, it contains the error message.
        callback: (success: boolean, bytes_or_err: any) -> (),
        --- Optional offset in the buffer to start writing (defaults to 0)
        offset: number?
    ) -> (),
    
    --- Writes data to a file asynchronously and executes a callback.
    writeFileWithCallback: (
        --- The path to the file
        filePath: string,
        --- The data to write (string or buffer)
        contents: string | buffer,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> ()
    ) -> (),
    
    --- Appends data to a file asynchronously and executes a callback.
    appendFileWithCallback: (
        --- The path to the file
        filePath: string,
        --- The data to append (string or buffer)
        contents: string | buffer,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> ()
    ) -> (),
    
    --- Copies a file to a new location asynchronously and executes a callback.
    copyFileWithCallback: (
        --- The path to the source file
        srcPath: string,
        --- The path to the destination
        destPath: string,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> ()
    ) -> (),
    
    --- Renames or moves a file or directory asynchronously and executes a callback.
    renameWithCallback: (
        --- The current path
        oldPath: string,
        --- The new path
        newPath: string,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> ()
    ) -> (),
    
    --- Checks if a path exists asynchronously and executes a callback.
    existsWithCallback: (
        --- The path to check
        filePath: string,
        --- Function called when complete. If success is true, exists_or_err is a boolean. Otherwise, it contains the error message.
        callback: (success: boolean, exists_or_err: any) -> ()
    ) -> (),
    
    --- Creates a directory asynchronously and executes a callback.
    mkdirWithCallback: (
        --- The path to the directory
        filePath: string,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> ()
    ) -> (),
    
    --- Reads the contents of a directory asynchronously and executes a callback.
    readDirWithCallback: (
        --- The path to the directory
        dirPath: string,
        --- Function called when complete. If success is true, files_or_err is an array of strings. Otherwise, it contains the error message.
        callback: (success: boolean, files_or_err: any) -> ()
    ) -> (),
    
    --- Retrieves file or directory information asynchronously and executes a callback.
    statWithCallback: (
        --- The path to query
        filePath: string,
        --- Function called when complete. If success is true, stat_or_err is a FileStat table. Otherwise, it contains the error message.
        callback: (success: boolean, stat_or_err: any) -> ()
    ) -> (),
    
    --- Resolves a path to its absolute, canonical form asynchronously and executes a callback.
    realpathWithCallback: (
        --- The path to resolve
        filePath: string,
        --- Function called when complete. If success is true, data_or_err contains the canonical path. Otherwise, it contains the error message.
        callback: (success: boolean, data_or_err: string) -> ()
    ) -> (),
    
    --- Removes a file or directory asynchronously and executes a callback.
    rmWithCallback: (
        --- The path to remove
        filePath: string,
        --- Function called when complete. If success is false, err contains the error message.
        callback: (success: boolean, err: string?) -> (),
        --- Optional boolean to remove directories recursively
        recursive: boolean?
    ) -> (),
}

return {} :: FileSystemModule
