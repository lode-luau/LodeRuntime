return {
    name = "sys",
    version = "1.0.0",
    description = "Lode System API",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "sys",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
