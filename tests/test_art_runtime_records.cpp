// Deterministic interleaving and failed-output controls. SPDX-License-Identifier: MIT
#include "../fixtures/art-runtime/record.h"
#include <cstdlib>
#include <string>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static std::string stream;
static unsigned calls;
static int adjustment;
static int64_t write_record(const void *data, size_t size) {
    ++calls;
    stream += "diagnostic before";
    stream.append(static_cast<const char *>(data), size);
    stream += "diagnostic after\n";
    return static_cast<int64_t>(size) + adjustment;
}
int main() {
    CHECK(artbox_runtime_record(write_record, "%s %d", "phase", 3));
    CHECK(calls == 1 && stream == "diagnostic before\nphase 3\ndiagnostic after\n");
    adjustment = -1;
    CHECK(!artbox_runtime_record(write_record, "short output"));
    adjustment = -2048;
    CHECK(!artbox_runtime_record(write_record, "failed output"));
    adjustment = 0;
    const unsigned previous = calls;
    std::string large(1023, 'x');
    CHECK(!artbox_runtime_record(write_record, "%s", large.c_str()) && calls == previous);
    large.resize(1022);
    CHECK(artbox_runtime_record(write_record, "%s", large.c_str()) && calls == previous + 1);
    CHECK(!artbox_runtime_record(nullptr, "no writer"));
    CHECK(!artbox_runtime_record(write_record, nullptr));
    std::puts("ART acceptance records: one write, exact boundaries and output failures verified");
}
