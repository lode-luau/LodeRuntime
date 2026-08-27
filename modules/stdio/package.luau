return {
    name = "stdio",
    version = "0.1.0",
    description = "Lode standard I/O module (Terminal & Stream Objects, Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "stdio",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
