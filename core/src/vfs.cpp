// Original rooted guest descriptor table, MIT.
#include "artbox/vfs.h"
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
struct descriptor {
    unsigned kind = 0, flags = 0;
    bool directory = false;
    void *handle = nullptr;
    std::string path;
    uint64_t position = 0;
};
struct artbox_vfs {
    std::mutex lock;
    artbox_file_ops files{};
    std::vector<descriptor> descriptors;
    std::vector<unsigned char> commandline;
};
extern "C" artbox_vfs *artbox_vfs_create(const artbox_file_ops *files, size_t limit) {
    if (!limit || limit > 4096 || (files && (!files->open || !files->close || !files->read || !files->write ||
        !files->seek || !files->stat || !files->stat_at))) return nullptr;
    artbox_vfs *fs = new (std::nothrow) artbox_vfs;
    if (!fs) return nullptr;
    if (files) fs->files = *files;
    try { fs->descriptors.resize(limit); }
    catch (const std::exception&) { delete fs; return nullptr; }
    return fs;
}
extern "C" int artbox_vfs_set_commandline(artbox_vfs *fs, const void *bytes, size_t length) {
    if (!fs || !bytes || !length) return -22;
    if (length > 65536) return -7;
    const auto *data = static_cast<const unsigned char*>(bytes);
    if (data[length - 1]) return -22;
    try {
        std::lock_guard<std::mutex> guard(fs->lock);
        if (!fs->commandline.empty()) return -114;
        fs->commandline.assign(data, data + length);
        return 0;
    } catch (const std::exception&) { return -12; }
}
extern "C" int artbox_vfs_destroy(artbox_vfs *fs) {
    if (!fs) return -22;
    int result = 0;
    for (descriptor &d : fs->descriptors) if (d.kind == 5) {
        int error = fs->files.close(d.handle);
        if (!result) result = error;
    }
    delete fs;
    return result;
}
static descriptor *get(artbox_vfs *fs, int32_t fd) {
    if (fd < 3 || static_cast<size_t>(fd - 3) >= fs->descriptors.size()) return nullptr;
    descriptor *d = &fs->descriptors[static_cast<size_t>(fd - 3)];
    return d->kind ? d : nullptr;
}
extern "C" int64_t artbox_vfs_mmap(artbox_vfs *fs, artbox_vm *vm, uint64_t address,
    uint64_t length, uint64_t prot, uint64_t flags, int64_t fd, uint64_t offset) {
    if (!fs || !vm) return -22;
    if (flags & 0x20) return artbox_vm_mmap(vm, address, length, prot, flags, fd, offset);
    if (prot & 4) return -1;
    std::lock_guard<std::mutex> guard(fs->lock);
    descriptor *d = get(fs, static_cast<int32_t>(fd));
    if (!d) return -9;
    if (d->kind != 5 || d->directory) return -19;
    unsigned access = d->flags & 3;
    if (access == 1) return -13; // Every file mapping requires readable backing.
    if (!fs->files.mapping.acquire) return -95;
    unsigned maximum = (flags & 0xf) == 1 && access == 0 ? 1u : 3u;
    return artbox_vm_map_file(vm, address, length, prot, flags, offset, d->handle, &fs->files.mapping, maximum);
}
struct path_info {
    std::string relative, canonical;
    void *directory = nullptr;
    bool trailing = false;
};
static int path(artbox_vfs *fs, artbox_vm *vm, uint64_t address, int32_t dirfd, path_info &out) {
    char bytes[4096]; size_t length = 0;
    for (; length < sizeof(bytes); ++length) {
        if (address > UINT64_MAX - length || artbox_vm_read(vm, address + length, bytes + length, 1)) return -14;
        if (!bytes[length]) break;
    }
    if (length == sizeof(bytes)) return -36;
    if (!length) return -2;
    out.trailing = bytes[length - 1] == '/';
    std::string prefix;
    if (bytes[0] != '/' && dirfd != -100) {
        descriptor *base = get(fs, dirfd);
        if (!base) return -9;
        if (!base->directory) return -20;
        out.directory = base->handle; prefix = base->path;
    }
    size_t cursor = 0;
    while (cursor < length) {
        while (cursor < length && bytes[cursor] == '/') ++cursor;
        size_t start = cursor;
        while (cursor < length && bytes[cursor] != '/') ++cursor;
        size_t size = cursor - start;
        if (!size || (size == 1 && bytes[start] == '.')) continue;
        if (size == 2 && bytes[start] == '.' && bytes[start + 1] == '.') return -1;
        if (!out.relative.empty()) out.relative += '/';
        out.relative.append(bytes + start, size);
    }
    out.canonical = prefix;
    if (!prefix.empty() && !out.relative.empty()) out.canonical += '/';
    out.canonical += out.relative;
    return out.canonical.size() >= sizeof(bytes) ? -36 : 0;
}
static unsigned device(const std::string &name) {
    return name == "dev/null" ? 1u : name == "dev/zero" ? 2u : name == "dev/urandom" ? 3u : name == "dev" ? 4u :
           name == "proc/self/cmdline" ? 6u : name == "proc" ? 7u : name == "proc/self" ? 8u : 0u;
}
static bool virtual_directory(unsigned kind) { return kind == 4 || kind == 7 || kind == 8; }
static bool below(const std::string &name, const char *prefix) {
    size_t n = std::strlen(prefix);
    return name.compare(0, n, prefix) == 0 && (name.size() == n || name[n] == '/');
}
struct Walk {
    artbox_vfs *fs;
    void *current, *owned = nullptr;
    std::string leaf;
    Walk(artbox_vfs *f, void *base) : fs(f), current(base) {}
    ~Walk() { if (owned) (void)fs->files.close(owned); }
    int resolve(const std::string &relative) {
        size_t begin = 0, slash;
        while ((slash = relative.find('/', begin)) != std::string::npos) {
            std::string component = relative.substr(begin, slash - begin);
            void *next = nullptr;
            int error = fs->files.open(fs->files.context, current, component.c_str(), 0x84000, 0, &next);
            if (error) return error;
            if (owned) {
                error = fs->files.close(owned);
                owned = next; current = next;
                if (error) return error;
            } else { owned = next; current = next; }
            begin = slash + 1;
        }
        leaf = relative.substr(begin);
        if (leaf.empty()) leaf = ".";
        return 0;
    }
};
static void put(unsigned char *out, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out[i] = static_cast<unsigned char>(value >> (8 * i));
}
static int stat_bytes(artbox_vm *vm, uint64_t address, const artbox_file_info &f) {
    unsigned char bytes[128] = {};
    put(bytes, f.device, 8); put(bytes + 8, f.inode, 8); put(bytes + 16, f.mode, 4);
    put(bytes + 20, f.links, 4); put(bytes + 24, f.uid, 4); put(bytes + 28, f.gid, 4);
    put(bytes + 32, f.rdevice, 8); put(bytes + 48, f.size, 8); put(bytes + 56, f.block_size, 4);
    put(bytes + 64, f.blocks, 8);
    put(bytes + 72, static_cast<uint64_t>(f.access_seconds), 8); put(bytes + 80, static_cast<uint64_t>(f.access_nanoseconds), 8);
    put(bytes + 88, static_cast<uint64_t>(f.modify_seconds), 8); put(bytes + 96, static_cast<uint64_t>(f.modify_nanoseconds), 8);
    put(bytes + 104, static_cast<uint64_t>(f.change_seconds), 8); put(bytes + 112, static_cast<uint64_t>(f.change_nanoseconds), 8);
    return artbox_vm_write(vm, address, bytes, sizeof(bytes));
}
static artbox_file_info device_info(unsigned kind) {
    artbox_file_info f{};
    f.device = 1; f.inode = kind; f.links = 1; f.block_size = 4096;
    if (kind >= 6) { f.uid = f.gid = 10000; f.block_size = 1024; }
    if (virtual_directory(kind)) { f.mode = 0040555; return f; }
    if (kind == 6) { f.mode = 0100444; return f; } // Linux proc inode size is zero.
    f.mode = 0020000u | (kind == 3 ? 0444u : 0666u);
    f.rdevice = 0x100u + (kind == 1 ? 3u : kind == 2 ? 5u : 9u);
    return f;
}
struct Transfer { artbox_vfs *fs; descriptor *file; bool writing; };
static int64_t transfer(void *context, void *buffer, size_t length) {
    Transfer *t = static_cast<Transfer*>(context);
    return t->writing ? t->fs->files.write(t->file->handle, buffer, length) :
                        t->fs->files.read(t->file->handle, buffer, length);
}
extern "C" int64_t artbox_vfs_call(artbox_vfs *fs, artbox_kernel_thread *thread, uint64_t number,
                                   uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!fs || !thread || !thread->vm || !thread->system.random) return -22;
    if (number != 48 && number != 56 && number != 57 && number != 62 && number != 63 && number != 64 && number != 79 && number != 80)
        return -38;
    try {
        std::lock_guard<std::mutex> guard(fs->lock);
        if (number == 48 || number == 56 || number == 79) {
            unsigned flags = static_cast<uint32_t>(a2);
            if (number == 48 && (flags & ~7u)) return -22;
            if (number == 79 && a3 != 0 && a3 != 0x100) return -95;
            path_info p;
            int error = path(fs, thread->vm, a1, static_cast<int32_t>(a0), p);
            if (error) return error;
            unsigned kind = device(p.canonical);
            if (!kind && (below(p.canonical, "dev") || below(p.canonical, "proc"))) return -2;
            if (kind == 6 && fs->commandline.empty()) return -2;
            if (!kind && !fs->files.open) return -38;
            if (kind && !virtual_directory(kind) && p.trailing) return -20;
            // /proc/self is a process alias. Following it is supported; exposing
            // its symlink metadata requires a later readlink/lstat contract.
            if (kind == 8 && number == 79 && a3 == 0x100) return -95;
            if (kind == 8 && number == 56 && (flags & 0x8000)) return (flags & 0x4000) ? -20 : -40;
            if (number == 48 || number == 79) {
                artbox_file_info info{};
                if (kind) info = device_info(kind);
                else {
                    Walk walk(fs, p.directory);
                    if ((error = walk.resolve(p.relative)) ||
                        (error = fs->files.stat_at(fs->files.context, walk.current, walk.leaf.c_str(), &info))) return error;
                    if (p.trailing && (info.mode & 0170000) != 0040000) return -20;
                }
                if (number == 79) return stat_bytes(thread->vm, a2, info);
                unsigned permissions = (info.mode >> 6) & 7;
                if (below(p.canonical, "system") && (flags & 2)) return -30;
                return (permissions & flags) == flags ? 0 : -13;
            }
            if ((flags & 3u) == 3u) return -22;
            if (flags & ~(3u | 0x40u | 0x80u | 0x100u | 0x200u | 0x400u | 0x800u | 0x8000u | 0x4000u | 0x20000u | 0x80000u)) return -95;
            if (kind && (flags & 0xc0) == 0xc0) return -17;
            if (kind && !virtual_directory(kind) && (flags & 0x4000)) return -20;
            if (virtual_directory(kind) && ((flags & 3) || (flags & 0x200))) return -21;
            if ((kind == 3 || kind == 6) && (flags & 3)) return -13;
            if (kind == 6 && (flags & 0x200)) return -13;
            if (below(p.canonical, "system") && ((flags & 3) || (flags & (0x40 | 0x200)))) return -30;
            size_t slot = 0;
            while (slot < fs->descriptors.size() && fs->descriptors[slot].kind) ++slot;
            if (slot == fs->descriptors.size()) return -24;
            descriptor d; d.kind = kind ? kind : 5; d.flags = flags; d.directory = virtual_directory(kind); d.path = p.canonical;
            if (!kind) {
                Walk walk(fs, p.directory);
                if ((error = walk.resolve(p.relative))) return error;
                unsigned native_flags = flags | (p.trailing ? 0x4000u : 0);
                if ((error = fs->files.open(fs->files.context, walk.current, walk.leaf.c_str(), native_flags,
                                             static_cast<uint32_t>(a3) & 0777u, &d.handle))) return error;
                artbox_file_info info{};
                if ((error = fs->files.stat(d.handle, &info))) { (void)fs->files.close(d.handle); return error; }
                d.directory = (info.mode & 0170000) == 0040000;
            }
            fs->descriptors[slot] = std::move(d);
            return static_cast<int64_t>(slot + 3);
        }
        descriptor *d = get(fs, static_cast<int32_t>(a0));
        if (!d) return -9;
        if (number == 57) {
            int error = d->kind == 5 ? fs->files.close(d->handle) : 0;
            *d = descriptor{}; return error;
        }
        if (number == 62) {
            if (d->kind == 6) {
                unsigned origin = static_cast<uint32_t>(a2);
                if (origin > 4) return -22;
                if (origin > 2) return -95; // SEEK_DATA/HOLE are outside this snapshot contract.
                int64_t offset = static_cast<int64_t>(a1), base = origin == 1 ? static_cast<int64_t>(d->position) : 0;
                if (offset < -base || offset > INT64_MAX - base) return -22;
                d->position = static_cast<uint64_t>(base + offset);
                return base + offset;
            }
            if (d->kind != 5) return static_cast<uint32_t>(a2) <= 4 ? 0 : -22;
            return fs->files.seek(d->handle, static_cast<int64_t>(a1), static_cast<uint32_t>(a2));
        }
        if (number == 80) {
            artbox_file_info info{};
            if (d->kind == 5) { int error = fs->files.stat(d->handle, &info); if (error) return error; }
            else info = device_info(d->kind);
            return stat_bytes(thread->vm, a1, info);
        }
        bool writing = number == 64;
        unsigned access = d->flags & 3;
        if ((writing && !access) || (!writing && access == 1)) return -9;
        if (d->directory) return -21;
        size_t page = artbox_vm_page_size(thread->vm);
        uint64_t maximum = static_cast<uint64_t>(INT32_MAX) & ~(static_cast<uint64_t>(page) - 1);
        uint64_t count = a2 > maximum ? maximum : a2;
        if (d->kind != 5 && writing) return static_cast<int64_t>(count);
        if (d->kind == 1) return 0;
        uint64_t done = 0;
        while (done < count) {
            if (d->kind == 6 && d->position >= fs->commandline.size()) break;
            if (a1 > UINT64_MAX - done) return done ? static_cast<int64_t>(done) : -14;
            uint64_t address = a1 + done;
            size_t chunk = page - static_cast<size_t>(address % page);
            if (chunk > count - done) chunk = static_cast<size_t>(count - done);
            int64_t result;
            if (d->kind == 6) {
                size_t remaining = fs->commandline.size() - static_cast<size_t>(d->position);
                if (chunk > remaining) chunk = remaining;
                int error = artbox_vm_write(thread->vm, address, fs->commandline.data() + d->position, chunk);
                if (!error) d->position += chunk;
                result = error ? error : static_cast<int64_t>(chunk);
            } else if (d->kind == 5) {
                Transfer t{fs, d, writing};
                result = artbox_vm_transfer(thread->vm, address, chunk, writing ? 1 : 2, transfer, &t);
                if (result == -14 && !writing && address <= static_cast<uint64_t>(INT64_MAX) - chunk) {
                    // A regular-file read at EOF copies no bytes, even when its
                    // destination is unmapped. Query without advancing the FD.
                    artbox_file_info info{};
                    int64_t position = fs->files.seek(d->handle, 0, 1);
                    if (position >= 0 && !fs->files.stat(d->handle, &info) && static_cast<uint64_t>(position) >= info.size)
                        result = 0;
                }
            } else {
                unsigned char bytes[256] = {};
                if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
                int error = d->kind == 3 ? thread->system.random(bytes, chunk) : 0;
                if (!error) error = artbox_vm_write(thread->vm, address, bytes, chunk);
                result = error ? error : static_cast<int64_t>(chunk);
            }
            if (result < 0) return done ? static_cast<int64_t>(done) : result;
            done += static_cast<uint64_t>(result);
            if (result < static_cast<int64_t>(chunk)) break;
        }
        return static_cast<int64_t>(done);
    } catch (const std::exception&) { return -12; }
}
