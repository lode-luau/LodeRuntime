-- Type definitions for the SIMD Module Native DLL
-- This file is for LSP autocomplete and type checking only.
-- At runtime, Lode will load the native binary specified in package.luau instead of this file.

export type SIMDModule = {
    --- Scales all floats in the buffer by the given scalar using AVX2.
    scale_floats: (buf: buffer, scalar: number) -> boolean
}

return {} :: SIMDModule
