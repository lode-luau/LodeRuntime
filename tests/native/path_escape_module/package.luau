return {
    name = "path_escape_module",
    version = "1.0.0",
    implementation = {
        artifact = "path_escape_module",
        layout = "../native_module/libs/windows/x64/native_module.dll",
        required = true,
        targets = {
            release = { "windows/x64" },
        },
    },
}
