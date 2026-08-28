return {
    name = "address", version = "1.0.0", description = "Opaque native address operations", author = "Lode Team", license = "MIT",
    implementation = { artifact = "address", required = true, targets = { build = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" }, release = { "windows/x64", "linux/x64", "macos/x64", "macos/arm64" } } },
}
