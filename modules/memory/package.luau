return {
    name = "memory",
    version = "1.0.0",
    description = "Lode runtime-owned native memory blocks",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "memory",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
