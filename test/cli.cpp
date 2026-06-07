#define FERNBUFFER_IMPORT_DLL
#include "../fernbuffer.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); exit(1); }
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf((size_t)sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static void write_file(const char* path, const void* data, size_t len) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "error: cannot write '%s'\n", path); exit(1); }
    f.write(reinterpret_cast<const char*>(data), (std::streamsize)len);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage: fernbuffer_cli <encode|decode> <infile> <outfile>\n");
        return 1;
    }

    const char* cmd     = argv[1];
    const char* infile  = argv[2];
    const char* outfile = argv[3];

    if (strcmp(cmd, "encode") == 0) {
        auto in = read_file(infile);
        std::string json_str(in.begin(), in.end());

        size_t out_len = 0;
        uint8_t* out = fernbuffer_encode(json_str.c_str(), json_str.size(), &out_len);
        if (!out) { fprintf(stderr, "error: encode failed (invalid JSON?)\n"); return 1; }

        write_file(outfile, out, out_len);
        fernbuffer_free(out);
        printf("encoded  %s  (%zu B)  ->  %s  (%zu B)\n", infile, json_str.size(), outfile, out_len);
        return 0;
    }

    if (strcmp(cmd, "decode") == 0) {
        auto in = read_file(infile);

        char* out = fernbuffer_decode(in.data(), in.size());
        if (!out) { fprintf(stderr, "error: decode failed\n"); return 1; }

        size_t out_sz = strlen(out);
        write_file(outfile, out, out_sz);
        fernbuffer_free(out);
        printf("decoded  %s  (%zu B)  ->  %s  (%zu B)\n", infile, in.size(), outfile, out_sz);
        return 0;
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    return 1;
}
