#include "artbox/vfs.h"
#include "artbox/native_files.h"
#include "artbox/native_system.h"
#include "artbox/native_vm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
static artbox_vfs *caller_fs;
static artbox_kernel_thread *caller_thread;
static int caller_errno;
extern "C" int64_t artbox_files_check(uint64_t);
extern "C" int *artbox_file_errno(void) { return &caller_errno; }
extern "C" int64_t artbox_file_syscall(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    int64_t result = artbox_kernel_call(caller_thread, n, a, b, c, d, e, f);
    if (result == -38) result = artbox_vfs_call(caller_fs, caller_thread, n, a, b, c, d);
    if (result < 0 && result >= -4095) { caller_errno = static_cast<int>(-result); return -1; }
    return result;
}
struct Node { std::string name; bool directory = false, link = false; uint32_t mode = 0644; std::vector<unsigned char> bytes; };
struct Mock { std::map<std::string, Node> nodes; size_t handles = 0, reads = 0, writes = 0; };
struct Handle { Mock *fs; Node *node; size_t position; unsigned flags; };
static std::string path(void *directory, const char *name) {
    std::string base = directory ? static_cast<Handle*>(directory)->node->name : "";
    if (!std::strcmp(name, ".")) return base;
    return base.empty() ? name : base + "/" + name;
}
static int info(Node *node, artbox_file_info *out) {
    if (node->link) return -40;
    *out = artbox_file_info{};
    out->mode = (node->directory ? 0040000u : 0100000u) | node->mode;
    out->uid = out->gid = 10000; out->links = 1; out->block_size = 4096;
    out->size = node->bytes.size(); return 0;
}
static int open_mock(void *context, void *directory, const char *name, uint32_t flags, uint32_t mode, void **out) {
    Mock *fs = static_cast<Mock*>(context);
    CHECK(name && !std::strchr(name, '/') && std::strcmp(name, ".."));
    std::string key = path(directory, name);
    auto found = fs->nodes.find(key);
    if (found != fs->nodes.end() && (flags & 0xc0) == 0xc0) return -17;
    if (found == fs->nodes.end()) {
        if (!(flags & 0x40)) return -2;
        Node node; node.name = key; node.mode = mode & ~022u;
        found = fs->nodes.emplace(key, std::move(node)).first;
    }
    Node *n = &found->second;
    if (n->link) return -40;
    if ((flags & 0x4000) && !n->directory) return -20;
    if (n->directory && ((flags & 3) || (flags & 0x200))) return -21;
    if (flags & 0x200) n->bytes.clear();
    *out = new Handle{fs, n, 0, flags}; ++fs->handles; return 0;
}
static int close_mock(void *file) { Handle *h = static_cast<Handle*>(file); --h->fs->handles; delete h; return 0; }
static int64_t read_mock(void *file, void *buffer, size_t count) {
    Handle *h = static_cast<Handle*>(file); ++h->fs->reads;
    size_t left = h->position < h->node->bytes.size() ? h->node->bytes.size() - h->position : 0;
    if (count > left) count = left;
    if (count) std::memcpy(buffer, h->node->bytes.data() + h->position, count);
    h->position += count; return static_cast<int64_t>(count);
}
static int64_t write_mock(void *file, const void *buffer, size_t count) {
    Handle *h = static_cast<Handle*>(file); ++h->fs->writes;
    if (h->flags & 0x400) h->position = h->node->bytes.size();
    if (h->position + count > h->node->bytes.size()) h->node->bytes.resize(h->position + count);
    if (count) std::memcpy(h->node->bytes.data() + h->position, buffer, count);
    h->position += count; return static_cast<int64_t>(count);
}
static int64_t seek_mock(void *file, int64_t offset, unsigned origin) {
    Handle *h = static_cast<Handle*>(file);
    if (origin > 2) return -22;
    int64_t base = origin == 0 ? 0 : static_cast<int64_t>(origin == 1 ? h->position : h->node->bytes.size());
    if (offset < -base || offset > INT64_MAX - base) return -22;
    h->position = static_cast<size_t>(base + offset); return base + offset;
}
static int stat_mock(void *file, artbox_file_info *out) { return info(static_cast<Handle*>(file)->node, out); }
static int stat_at_mock(void *context, void *directory, const char *name, artbox_file_info *out) {
    Mock *fs = static_cast<Mock*>(context); auto it = fs->nodes.find(path(directory, name));
    return it == fs->nodes.end() ? -2 : info(&it->second, out);
}
static uint64_t word(const unsigned char *p, unsigned size) {
    uint64_t value = 0; for (unsigned i = 0; i < size; ++i) value |= static_cast<uint64_t>(p[i]) << (8*i); return value;
}
static void contract(const artbox_file_ops &files) {
    artbox_vm_ops memory = artbox_native_vm(); artbox_system_ops system = artbox_native_system();
    artbox_vm *vm = artbox_vm_create(&memory, 32 * memory.page_size, 16);
    artbox_kernel_thread thread;
    CHECK(vm && artbox_kernel_thread_init(&thread, vm, &system, 10000, 10000) == 0);
    int64_t mapped = artbox_vm_mmap(vm, 0, 4 * memory.page_size, 3, 0x22, -1, 0); CHECK(mapped > 0);
    uint64_t address = static_cast<uint64_t>(mapped), data = address + memory.page_size, boundary = data + memory.page_size;
    artbox_vfs *fs = artbox_vfs_create(&files, 32); CHECK(fs);
    caller_fs = fs; caller_thread = &thread;
    int64_t cases = artbox_files_check(memory.page_size);
    std::printf("File syscall caller: %lld\n", static_cast<long long>(cases));
    CHECK(cases == 41);
    auto call = [&](uint64_t n, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0, uint64_t d = 0) {
        return artbox_vfs_call(fs, &thread, n, a, b, c, d);
    };
    auto name = [&](const char *text) {
        CHECK(artbox_vm_write(vm, address, text, std::strlen(text) + 1) == 0); return address;
    };
    auto open = [&](const char *text, unsigned flags, int64_t dir = -100) { return call(56, static_cast<uint64_t>(dir), name(text), flags, 0644); };
    int64_t null = open("/dev/null", 2), file = open("/data/file", 0xc2), dir = open("/data", 0x4000);
    CHECK(null >= 3 && file >= 3 && dir >= 3 && null != file && file != dir && dir != null);
    CHECK(open("/data/file", 0xc2) == -17);
    CHECK(open("file", 0, file) == -20 && open("file", 0, 99) == -9);
    int64_t relative = open("./file", 0, dir); CHECK(relative >= 3);
    CHECK(call(57, static_cast<uint64_t>(relative)) == 0);
    relative = open("/data/file", 0, 99); CHECK(relative >= 3 && call(57, static_cast<uint64_t>(relative)) == 0);
    CHECK(open("/data/../escape", 0x42) == -1 && open("../escape", 0x42, dir) == -1);
    CHECK(open("/system/new", 0x42) == -30 && open("/system/readonly", 1) == -30);
    CHECK(open("/data/link", 0x202) == -40);
    int64_t jump = open("/data/jump/guard", 0x42); CHECK(jump == -20 || jump == -40);
    CHECK(open("/missing/file", 0x42) == -2 && open("/data/file/", 0) == -20);
    CHECK(call(48, UINT64_MAX - 99, name("/data/file"), 6) == 0);
    CHECK(call(48, UINT64_MAX - 99, name("/missing"), 0) == -2);
    const char text[] = "abcdefghij";
    CHECK(artbox_vm_write(vm, data, text, 10) == 0 && call(64, static_cast<uint64_t>(file), data, 10) == 10);
    CHECK(call(62, static_cast<uint64_t>(file), 0, 1) == 10);
    CHECK(call(80, static_cast<uint64_t>(file), data + 128) == 0);
    unsigned char stat[128]; CHECK(artbox_vm_read(vm, data + 128, stat, sizeof(stat)) == 0);
    CHECK(word(stat + 16, 4) == 0100644 && word(stat + 24, 4) == 10000 && word(stat + 48, 8) == 10);
    CHECK(call(79, static_cast<uint64_t>(dir), name("file"), data + 256, 0) == 0);
    CHECK(call(62, static_cast<uint64_t>(file), 0, 0) == 0);
    CHECK(call(63, static_cast<uint64_t>(file), 0, 10) == -14 && call(62, static_cast<uint64_t>(file), 0, 1) == 0);
    CHECK(call(63, static_cast<uint64_t>(file), data + 32, 16) == 10);
    unsigned char readback[10]; CHECK(artbox_vm_read(vm, data + 32, readback, 10) == 0 && !std::memcmp(readback, text, 10));
    CHECK(call(63, static_cast<uint64_t>(file), data + 32, 1) == 0);
    CHECK(call(63, static_cast<uint64_t>(file), 0, 1) == 0); // EOF never accesses a destination byte.
    CHECK(call(62, static_cast<uint64_t>(file), UINT64_MAX, 0) == -22);
    CHECK(call(62, static_cast<uint64_t>(file), 0, 0) == 0);
    CHECK(artbox_vm_mprotect(vm, boundary, memory.page_size, 0) == 0);
    CHECK(call(63, static_cast<uint64_t>(file), boundary - 2, 4) == 2 && call(62, static_cast<uint64_t>(file), 0, 1) == 2);
    CHECK(call(64, static_cast<uint64_t>(file), boundary - 2, 4) == 2 && call(62, static_cast<uint64_t>(file), 0, 1) == 4);
    CHECK(call(64, static_cast<uint64_t>(file), boundary, 1) == -14 && call(62, static_cast<uint64_t>(file), 0, 1) == 4);
    CHECK(call(63, static_cast<uint64_t>(dir), data, 0) == -21);
    CHECK(call(63, static_cast<uint64_t>(file), 0, 0) == 0);
    CHECK(call(57, static_cast<uint64_t>(file)) == 0 && call(57, static_cast<uint64_t>(file)) == -9);
    file = open("/data/file", 0x202); CHECK(file >= 3 && call(62, static_cast<uint64_t>(file), 0, 2) == 0);
    CHECK(call(57, static_cast<uint64_t>(file)) == 0);
    file = open("/data/append", 0x442); CHECK(file >= 3);
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i) {
        int64_t page = artbox_vm_mmap(vm, 0, memory.page_size, 3, 0x22, -1, 0); CHECK(page > 0);
        uint32_t record = i; CHECK(artbox_vm_write(vm, static_cast<uint64_t>(page), &record, sizeof(record)) == 0);
        workers.emplace_back([&, page] {
            for (unsigned j = 0; j < 64; ++j) CHECK(call(64, static_cast<uint64_t>(file), static_cast<uint64_t>(page), 4) == 4);
            CHECK(artbox_vm_munmap(vm, static_cast<uint64_t>(page), memory.page_size) == 0);
        });
    }
    for (std::thread &w : workers) w.join();
    CHECK(call(62, static_cast<uint64_t>(file), 0, 2) == 2048 && call(62, static_cast<uint64_t>(file), 0, 0) == 0);
    CHECK(call(63, static_cast<uint64_t>(file), data, 2048) == 2048);
    uint32_t records[512]; unsigned counts[8] = {};
    CHECK(artbox_vm_read(vm, data, records, sizeof(records)) == 0);
    for (uint32_t record : records) { CHECK(record < 8); ++counts[record]; }
    for (unsigned count : counts) CHECK(count == 64);
    CHECK(artbox_vfs_destroy(fs) == 0 && artbox_vm_destroy(vm) == 0);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    Mock mock;
    for (const char *name : {"", "data", "system"}) { Node n; n.name = name; n.directory = true; n.mode = 0755; mock.nodes.emplace(name, n); }
    for (const char *name : {"data/link", "data/jump"}) { Node n; n.name = name; n.link = true; mock.nodes.emplace(name, n); }
    Node ro; ro.name = "system/readonly"; mock.nodes.emplace(ro.name, ro);
    artbox_file_ops files{&mock, open_mock, close_mock, read_mock, write_mock, seek_mock, stat_mock, stat_at_mock};
    contract(files); CHECK(mock.handles == 0);
#if defined(__linux__)
    int root = ::open(argv[1], O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    CHECK(root >= 0);
    int oracle = ::openat(root, "data/oracle", O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(oracle >= 0 && ::write(oracle, "abcd", 4) == 4);
    CHECK(::syscall(SYS_read, oracle, 0, 1) == 0); // Same invalid pointer at EOF.
    CHECK(::lseek(oracle, 0, SEEK_SET) == 0);
    CHECK(::syscall(SYS_read, oracle, 0, 1) == -1 && errno == EFAULT && ::lseek(oracle, 0, SEEK_CUR) == 0);
    CHECK(::syscall(SYS_read, root, 0, 0) == -1 && errno == EISDIR);
    CHECK(::close(oracle) == 0 && ::close(root) == 0);
#endif
    artbox_native_files *native = nullptr;
#if defined(_WIN32)
    CHECK(artbox_native_files_open(argv[1], &native) == -95 && !native);
#else
    CHECK(artbox_native_files_open(argv[1], &native) == 0);
    contract(artbox_native_files_ops(native));
    CHECK(artbox_native_files_close(native) == 0);
#endif
    std::puts("Rooted VFS: mixed descriptors, paths, file flags, stat, partial I/O and 512 concurrent appends passed");
    return 0;
}
