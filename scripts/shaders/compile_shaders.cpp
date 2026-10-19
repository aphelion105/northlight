// Standalone build tool. This program never loads the game or a rendering device.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
    if (argc != 5 && !(argc == 6 && std::strcmp(argv[5], "--native") == 0)) {
        std::fprintf(stderr, "Usage: compile_shaders.exe input.hlsl entry target output.bin [--native]\n");
        return 2;
    }
    FILE *input = std::fopen(argv[1], "rb");
    if (!input) { std::perror(argv[1]); return 3; }
    std::fseek(input, 0, SEEK_END);
    long size = std::ftell(input);
    std::rewind(input);
    if (size <= 0) { std::fclose(input); return 3; }
    char *source = static_cast<char *>(std::malloc(static_cast<size_t>(size)));
    if (!source || std::fread(source, 1, size, input) != static_cast<size_t>(size)) {
        std::fclose(input); std::free(source); return 3;
    }
    std::fclose(input);
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) { std::fprintf(stderr, "LoadLibrary d3dcompiler_47.dll: %lu\n", GetLastError()); std::free(source); return 4; }
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) { std::free(source); FreeLibrary(compiler); return 4; }
    ID3DBlob *shader = nullptr, *diagnostics = nullptr;
    HRESULT result = compile(source, size, argv[1], nullptr, nullptr, argv[2], argv[3],
        (argc == 6 ? D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_IEEE_STRICTNESS
                   : D3DCOMPILE_ENABLE_STRICTNESS) | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shader, &diagnostics);
    std::free(source);
    if (diagnostics) {
        std::fwrite(diagnostics->GetBufferPointer(), 1, diagnostics->GetBufferSize(), stderr);
        diagnostics->Release();
    }
    if (FAILED(result) || !shader) {
        std::fprintf(stderr, "D3DCompile %s/%s failed: 0x%08lx\n", argv[2], argv[3], static_cast<unsigned long>(result));
        FreeLibrary(compiler); return 5;
    }
    FILE *output = std::fopen(argv[4], "wb");
    if (!output) { std::perror(argv[4]); shader->Release(); FreeLibrary(compiler); return 6; }
    size_t written = std::fwrite(shader->GetBufferPointer(), 1, shader->GetBufferSize(), output);
    int close_result = std::fclose(output);
    bool success = written == shader->GetBufferSize() && close_result == 0;
    using Disassemble = HRESULT (WINAPI *)(const void *, SIZE_T, UINT, const char *, ID3DBlob **);
    auto disassemble = reinterpret_cast<Disassemble>(GetProcAddress(compiler, "D3DDisassemble"));
    ID3DBlob *assembly = nullptr;
    if (disassemble && SUCCEEDED(disassemble(shader->GetBufferPointer(), shader->GetBufferSize(),
            D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING, nullptr, &assembly)) && assembly) {
        char path[MAX_PATH * 4];
        std::snprintf(path, sizeof(path), "%s.asm", argv[4]);
        FILE *asm_file = std::fopen(path, "wb");
        if (asm_file) {
            std::fwrite(assembly->GetBufferPointer(), 1, assembly->GetBufferSize(), asm_file);
            std::fclose(asm_file);
        }
        assembly->Release();
    }
    std::printf("%s %s: %zu bytes\n", argv[2], argv[3], static_cast<size_t>(shader->GetBufferSize()));
    shader->Release(); FreeLibrary(compiler);
    return success ? 0 : 6;
}
