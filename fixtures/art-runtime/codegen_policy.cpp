// Original ARTBox diagnostic. MIT.
#include "no_codegen.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <thread>

int main(int argc, char** argv) {
  if (argc != 2) return 64;
  const long size = sysconf(_SC_PAGESIZE);
  const char* mode = argv[1];
  if (size <= 0) return 2;
  if (std::strcmp(mode, "existing-thread-exec") == 0) {
    std::atomic<int> command{0};
    std::thread worker([&] {
      while (command.load(std::memory_order_acquire) == 0) std::this_thread::yield();
      if (command.load(std::memory_order_relaxed) == 1)
        (void)mmap(nullptr, size, PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    });
    if (!artbox_deny_runtime_codegen()) {
      command.store(2, std::memory_order_release); worker.join();
      std::perror("codegen policy"); return 2;
    }
    command.store(1, std::memory_order_release); worker.join();
    return 8;
  }
  if (!artbox_deny_runtime_codegen()) { std::perror("codegen policy"); return 2; }
  if (std::strcmp(mode, "allow") == 0) {
    if (personality(0xffffffffUL) == -1) return 9;
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return 3;
    *static_cast<int*>(memory) = 7;
    std::thread worker([&] { *static_cast<int*>(memory) += 4; });
    worker.join();
    if (mprotect(memory, size, PROT_READ) != 0 || *static_cast<int*>(memory) != 11) return 4;
    if (munmap(memory, size) != 0) return 5;
    std::puts("ARTBox: native code, data mappings and threads remain usable");
    return 0;
  }
  if (std::strcmp(mode, "anonymous-exec") == 0)
    (void)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  else if (std::strcmp(mode, "file-exec") == 0) {
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 6;
    (void)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
  } else if (std::strcmp(mode, "protect-exec") == 0) {
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) return 7;
    (void)mprotect(memory, size, PROT_READ | PROT_EXEC);
  } else if (std::strcmp(mode, "thread-exec") == 0) {
    std::thread worker([&] { (void)mmap(nullptr, size, PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); });
    worker.join();
  } else if (std::strcmp(mode, "execve") == 0) {
    char* arguments[] = {argv[0], const_cast<char*>("allow"), nullptr};
    char* environment[] = {nullptr};
    (void)execve(argv[0], arguments, environment);
  } else if (std::strcmp(mode, "execveat") == 0) {
    char* arguments[] = {argv[0], const_cast<char*>("allow"), nullptr};
    char* environment[] = {nullptr};
    (void)syscall(__NR_execveat, AT_FDCWD, argv[0], arguments, environment, 0);
  } else if (std::strcmp(mode, "shared-exec") == 0) {
    // Denial precedes kernel validation of the deliberately invalid segment.
    (void)syscall(__NR_shmat, -1, nullptr, SHM_EXEC);
  } else if (std::strcmp(mode, "pkey-exec") == 0) {
#ifdef __NR_pkey_mprotect
    (void)syscall(__NR_pkey_mprotect, nullptr, size, PROT_EXEC, -1);
#else
    return 65;
#endif
  } else if (std::strcmp(mode, "personality") == 0) {
    (void)personality(READ_IMPLIES_EXEC);
  } else return 64;
  std::fputs("ARTBox: forbidden operation returned to its caller\n", stderr);
  return 8;
}
