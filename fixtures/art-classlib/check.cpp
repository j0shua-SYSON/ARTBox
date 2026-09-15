// Original ARTBox contract for the pinned implementation class libraries.
// Uses the AOSP DEX verifier and metadata APIs; never starts ART.
#include "dex/dex_file_loader.h"
#include "dex/dex_file-inl.h"
#include "dex/class_accessor-inl.h"
#include "base/mem_map.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static int inspect(const unsigned char* const* inputs, const size_t* sizes,
                   size_t count, size_t* classes) {
    std::set<std::string> descriptors;
    bool object_fields = false;
    for (size_t i = 0; i < count; ++i) {
        if (!inputs[i] || sizes[i] < 112 || sizes[i] > 32 * 1024 * 1024 ||
            reinterpret_cast<uintptr_t>(inputs[i]) % 4) return 1;
        std::string error;
        art::DexFileLoader loader(inputs[i], sizes[i], "ARTBoxClassLibrary.dex");
        auto dex = loader.Open(0, true, true, &error);
        if (!dex) {
            std::fprintf(stderr, "AOSP DEX verifier: %s\n", error.c_str());
            return 2;
        }
        for (size_t j = 0; j < dex->NumClassDefs(); ++j) {
            const auto& cls = dex->GetClassDef(j);
            std::string descriptor = dex->GetClassDescriptor(cls);
            if (!descriptors.insert(descriptor).second) {
                std::fprintf(stderr, "Class-library contract: duplicate class %s\n", descriptor.c_str());
                return 3;
            }
            if (descriptor == "Ljava/lang/Object;") {
                art::ClassAccessor accessor(*dex, cls);
                std::map<std::string, std::string> fields;
                for (const auto& field : accessor.GetInstanceFields()) {
                    if (field.GetAccessFlags() != (art::kAccPrivate | art::kAccTransient)) return 3;
                    fields.emplace(dex->GetFieldName(field.GetIndex()), dex->GetFieldTypeDescriptor(field.GetIndex()));
                }
                const std::map<std::string, std::string> expected = {
                    {"shadow$_klass_", "Ljava/lang/Class;"}, {"shadow$_monitor_", "I"}};
                if (fields != expected || accessor.NumStaticFields() != 0) {
                    std::fprintf(stderr, "Class-library contract: Object field metadata differs\n");
                    return 3;
                }
                object_fields = true;
            }
        }
    }
    *classes = descriptors.size();
    const char* required[] = {
        "Ljava/lang/Object;", "Ljava/lang/Class;", "Ljava/lang/String;", "Ljava/lang/Thread;",
        "Ldalvik/system/VMRuntime;", "Ljava/lang/ref/Reference;", "Ljava/lang/Throwable;",
        "Landroid/icu/util/ULocale;", "Lcom/android/org/conscrypt/NativeCrypto;",
        "Lcom/android/okhttp/OkHttpClient;"};
    for (const char* name : required) {
        if (!descriptors.count(name)) {
            std::fprintf(stderr, "Class-library contract: missing %s\n", name);
            return 3;
        }
    }
    if (count != 2 || descriptors.size() != 7309 || !object_fields) {
        std::fprintf(stderr, "Class-library contract: incomplete class set\n");
        return 3;
    }
    return 0;
}

extern "C" __attribute__((visibility("default")))
int artbox_classlib_inspect(const unsigned char* const* inputs, const size_t* sizes,
                           size_t count, size_t* classes) {
    if (!inputs || !sizes || !classes || !count || count > 3) return 1;
    *classes = 0;
    art::MemMap::Init();
    int result = inspect(inputs, sizes, count, classes);
    art::MemMap::Shutdown();
    return result;
}

#ifdef ARTBOX_CLASSLIB_EXECUTABLE
int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) return 1;
    std::vector<std::vector<unsigned char>> data;
    std::vector<const unsigned char*> inputs;
    std::vector<size_t> sizes;
    for (int i = 1; i < argc; ++i) {
        FILE* file = std::fopen(argv[i], "rb");
        if (!file) return 1;
        if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return 1; }
        long size = std::ftell(file);
        if (size < 0 || size > 32 * 1024 * 1024 || std::fseek(file, 0, SEEK_SET)) {
            std::fclose(file); return 1;
        }
        data.emplace_back(static_cast<size_t>(size));
        bool read = std::fread(data.back().data(), 1, size, file) == static_cast<size_t>(size);
        int closed = std::fclose(file);
        if (!read || closed) return 1;
    }
    for (const auto& bytes : data) { inputs.push_back(bytes.data()); sizes.push_back(bytes.size()); }
    size_t classes = 0;
    int result = artbox_classlib_inspect(inputs.data(), sizes.data(), inputs.size(), &classes);
    std::printf("{\"dex_verified\":%s,\"dex_executed\":false,\"classes\":%zu,\"result\":%d}\n",
                result == 0 ? "true" : "false", classes, result);
    return result;
}
#endif
