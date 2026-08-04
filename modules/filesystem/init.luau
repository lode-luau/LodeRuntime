--!strict
--!native

--[[
    Filesystem Module (fs).
    Provides functions for interacting with the file system.
    Operations are executed asynchronously in libuv's thread pool.
]]

export type PathModule = {
    --- Resolves paths into an absolute path.
    --- @param ... The path segments to resolve
    --- @return The resolved absolute path
    resolve: (...string) -> string,
    
    --- Joins path segments together.
    --- @param ... The path segments to join
    --- @return The joined path
    join: (...string) -> string,
    
    --- Returns the directory name of a path.
    --- @param p The path
    --- @return The directory name
    dirname: (p: string) -> string,
    
    --- Returns the base name of a path.
    --- @param p The path
    --- @return The base name
    basename: (p: string) -> string,
    
    --- Returns the extension of a path.
    --- @param p The path
    --- @return The extension
    extname: (p: string) -> string,
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

export type FileSystemModule = {
    --- Path utilities
    path: PathModule,

    -- =======================================================
    -- Coroutine-Yielding Methods
    -- Suspends current coroutine and resumes when ready.
    -- On error, throws a lua error catchable via pcall.
    -- =======================================================

    --- Reads a file asynchronously and returns its contents as a string.
    --- @param filePath The path to the file
    --- @return The file contents
    readFile: (filePath: string) -> string,

    --- Reads a file asynchronously and returns its contents as a buffer.
    --- @param filePath The path to the file
    --- @return The file contents as a buffer
    readFileBuffer: (filePath: string) -> buffer,

    --- Reads a file directly into a pre-allocated buffer with zero memory allocations.
    --- @param filePath The path to the file
    --- @param buf The buffer to write the data into
    --- @param offset Optional offset in the buffer to start writing (defaults to 0)
    --- @return The number of bytes read
    readToBuffer: (filePath: string, buf: buffer, offset: number?) -> number,

    --- Writes data to a file asynchronously, replacing its contents if it exists.
    --- @param filePath The path to the file
    --- @param contents The data to write (string or buffer)
    writeFile: (filePath: string, contents: string | buffer) -> (),

    --- Appends data to a file asynchronously, creating the file if it does not exist.
    --- @param filePath The path to the file
    --- @param contents The data to append (string or buffer)
    appendFile: (filePath: string, contents: string | buffer) -> (),

    --- Copies a file to a new location asynchronously. Overwrites the destination if it exists.
    --- @param srcPath The path to the source file
    --- @param destPath The path to the destination
    copyFile: (srcPath: string, destPath: string) -> (),

    --- Renames or moves a file or directory asynchronously.
    --- @param oldPath The current path
    --- @param newPath The new path
    rename: (oldPath: string, newPath: string) -> (),

    --- Checks if a path exists asynchronously.
    --- @param filePath The path to check
    --- @return True if the path exists, false otherwise
    exists: (filePath: string) -> boolean,

    --- Creates a directory and any necessary subdirectories asynchronously.
    --- @param filePath The path to the directory
    mkdir: (filePath: string) -> (),

    --- Reads the contents of a directory asynchronously.
    --- @param dirPath The path to the directory
    --- @return An array of strings representing the names of files and directories
    readDir: (dirPath: string) -> { string },

    --- Retrieves file or directory information asynchronously.
    --- @param filePath The path to query
    --- @return A FileStat table containing metadata
    stat: (filePath: string) -> FileStat,

    --- Resolves a path to its absolute, canonical form, resolving symlinks asynchronously.
    --- @param filePath The path to resolve
    --- @return The canonical path
    realpath: (filePath: string) -> string,

    --- Removes a file or directory asynchronously.
    --- @param filePath The path to remove
    --- @param recursive Optional boolean to remove directories recursively
    rm: (filePath: string, recursive: boolean?) -> (),

    -- =======================================================
    -- Callback-Based Methods
    -- Callback receives a boolean success indicator.
    -- If success is true, second arg is the data (if applicable).
    -- If success is false, second arg is the error message.
    -- =======================================================

    --- Reads a file asynchronously and executes a callback with its contents as a string.
    --- @param filePath The path to the file
    --- @param callback Function called when complete. If success is true, data_or_err contains the string data. Otherwise, it contains the error message.
    readFileWithCallback: (filePath: string, callback: (success: boolean, data_or_err: string) -> ()) -> (),
    
    --- Reads a file asynchronously and executes a callback with its contents as a buffer.
    --- @param filePath The path to the file
    --- @param callback Function called when complete. If success is true, data_or_err contains the buffer. Otherwise, it contains the error message.
    readFileBufferWithCallback: (filePath: string, callback: (success: boolean, data_or_err: any) -> ()) -> (),
    
    --- Reads a file directly into a pre-allocated buffer asynchronously and executes a callback.
    --- @param filePath The path to the file
    --- @param buf The buffer to write the data into
    --- @param callback Function called when complete. If success is true, bytes_or_err contains the number of bytes read. Otherwise, it contains the error message.
    --- @param offset Optional offset in the buffer to start writing (defaults to 0)
    readToBufferWithCallback: (filePath: string, buf: buffer, callback: (success: boolean, bytes_or_err: any) -> (), offset: number?) -> (),
    
    --- Writes data to a file asynchronously and executes a callback.
    --- @param filePath The path to the file
    --- @param contents The data to write (string or buffer)
    --- @param callback Function called when complete. If success is false, err contains the error message.
    writeFileWithCallback: (filePath: string, contents: string | buffer, callback: (success: boolean, err: string?) -> ()) -> (),
    
    --- Appends data to a file asynchronously and executes a callback.
    --- @param filePath The path to the file
    --- @param contents The data to append (string or buffer)
    --- @param callback Function called when complete. If success is false, err contains the error message.
    appendFileWithCallback: (filePath: string, contents: string | buffer, callback: (success: boolean, err: string?) -> ()) -> (),
    
    --- Copies a file to a new location asynchronously and executes a callback.
    --- @param srcPath The path to the source file
    --- @param destPath The path to the destination
    --- @param callback Function called when complete. If success is false, err contains the error message.
    copyFileWithCallback: (srcPath: string, destPath: string, callback: (success: boolean, err: string?) -> ()) -> (),
    
    --- Renames or moves a file or directory asynchronously and executes a callback.
    --- @param oldPath The current path
    --- @param newPath The new path
    --- @param callback Function called when complete. If success is false, err contains the error message.
    renameWithCallback: (oldPath: string, newPath: string, callback: (success: boolean, err: string?) -> ()) -> (),
    
    --- Checks if a path exists asynchronously and executes a callback.
    --- @param filePath The path to check
    --- @param callback Function called when complete. If success is true, exists_or_err is a boolean. Otherwise, it contains the error message.
    existsWithCallback: (filePath: string, callback: (success: boolean, exists_or_err: any) -> ()) -> (),
    
    --- Creates a directory asynchronously and executes a callback.
    --- @param filePath The path to the directory
    --- @param callback Function called when complete. If success is false, err contains the error message.
    mkdirWithCallback: (filePath: string, callback: (success: boolean, err: string?) -> ()) -> (),
    
    --- Reads the contents of a directory asynchronously and executes a callback.
    --- @param dirPath The path to the directory
    --- @param callback Function called when complete. If success is true, files_or_err is an array of strings. Otherwise, it contains the error message.
    readDirWithCallback: (dirPath: string, callback: (success: boolean, files_or_err: any) -> ()) -> (),
    
    --- Retrieves file or directory information asynchronously and executes a callback.
    --- @param filePath The path to query
    --- @param callback Function called when complete. If success is true, stat_or_err is a FileStat table. Otherwise, it contains the error message.
    statWithCallback: (filePath: string, callback: (success: boolean, stat_or_err: any) -> ()) -> (),
    
    --- Resolves a path to its absolute, canonical form asynchronously and executes a callback.
    --- @param filePath The path to resolve
    --- @param callback Function called when complete. If success is true, data_or_err contains the canonical path. Otherwise, it contains the error message.
    realpathWithCallback: (filePath: string, callback: (success: boolean, data_or_err: string) -> ()) -> (),
    
    --- Removes a file or directory asynchronously and executes a callback.
    --- @param filePath The path to remove
    --- @param callback Function called when complete. If success is false, err contains the error message.
    --- @param recursive Optional boolean to remove directories recursively
    rmWithCallback: (filePath: string, callback: (success: boolean, err: string?) -> (), recursive: boolean?) -> (),
}

return {} :: FileSystemModule
