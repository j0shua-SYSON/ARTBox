// Original ARTBox diagnostic. MIT. Native Linux reference only.
#pragma once
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <unistd.h>

static void artbox_codegen_denied(int, siginfo_t* info, void*) {
  static const char message[] = "ARTBox: denied executable mapping or process execution\n";
  (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
  // Keep diagnostics async-signal-safe and identify the rejected system call.
  char number[16];
  unsigned int value = static_cast<unsigned int>(info->si_syscall);
  unsigned int index = sizeof(number);
  number[--index] = '\n';
  do { number[--index] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
  (void)!write(STDERR_FILENO, number + index, sizeof(number) - index);
  _exit(126);
}

// A matching call either traps or returns ALLOW; an unmatched call preserves
// the accumulator's syscall number for the next rule.
#define ARTBOX_DENY_PERMISSION(number, mask) \
  BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 4), \
  BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])), \
  BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, mask, 0, 1), \
  BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP), \
  BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

static bool artbox_deny_runtime_codegen() {
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The diagnostic filter requires a little-endian native target
#endif
#if defined(__aarch64__)
  constexpr unsigned int architecture = AUDIT_ARCH_AARCH64;
#elif defined(__x86_64__)
  constexpr unsigned int architecture = AUDIT_ARCH_X86_64;
#else
#error Unsupported native Linux reference architecture
#endif
  const int current_personality = personality(0xffffffffUL);
  if (current_personality == -1 || (current_personality & READ_IMPLIES_EXEC)) {
    errno = EPERM;
    return false;
  }
  struct sigaction action{};
  action.sa_sigaction = artbox_codegen_denied;
  action.sa_flags = SA_SIGINFO;
  if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGSYS, &action, nullptr) != 0) return false;
  struct sock_filter rules[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#if defined(__x86_64__)
    BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_personality, 0, 4),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0xffffffffU, 1, 0),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execveat, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
    ARTBOX_DENY_PERMISSION(__NR_mmap, PROT_EXEC),
    ARTBOX_DENY_PERMISSION(__NR_mprotect, PROT_EXEC),
#ifdef __NR_pkey_mprotect
    ARTBOX_DENY_PERMISSION(__NR_pkey_mprotect, PROT_EXEC),
#endif
    ARTBOX_DENY_PERMISSION(__NR_shmat, SHM_EXEC),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog program{static_cast<unsigned short>(sizeof(rules) / sizeof(rules[0])), rules};
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) return false;
  return syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &program) == 0;
}
#undef ARTBOX_DENY_PERMISSION
