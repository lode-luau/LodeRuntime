return {
    name = "crypto",
    version = "0.1.0",
    description = "Lode cryptography module with native platform providers",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "crypto",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
