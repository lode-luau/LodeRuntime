return {
    name = "ffi",
    version = "0.1.0",
    description = "Lode foreign function interface: call C symbols from any dynamic library",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "ffi",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
