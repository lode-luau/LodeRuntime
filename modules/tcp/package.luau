return {
    name = "tcp",
    version = "0.1.0",
    description = "Lode TCP module (Async/LibUV)",
    author = "Lode Team",
    license = "MIT",
    implementation = {
        artifact = "tcp",
        required = true,
        targets = {
            build = { "windows/x64" },
            release = { "windows/x64" },
        },
    },
}
