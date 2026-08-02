-- Module Nativo: Type definitions para LSP, autocomplete e documentação
-- O LodeRuntime carrega a DLL nativa diretamente via lode.json.

export type NativeModule = {
    greet: (name: string?) -> string,
    square: (x: number) -> number,
    getSystemInfo: () -> { platform: string, arch: string }
}

return {} :: NativeModule
