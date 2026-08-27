return {
    name = "crypto",
    version = "0.1.0",
    description = "Lode cryptography module with platform-native providers",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "crypto",
        required = true,
        targets = {
            build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
            release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" },
        },
    },
}
