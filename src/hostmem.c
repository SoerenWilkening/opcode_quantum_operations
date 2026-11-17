/* Host memory access that never faults (BACKEND_ABI §1.10 "No fault", CR §2). The kernel does
 * the access and reports a bad address as an error: no signal handler (process-global, not
 * thread- or library-friendly), no probe-then-touch race, and nothing ASan instruments, so a
 * bus access may read any readable byte, redzones included, as the ABI asks.
 * shortcut: one system call per access read and per byte written, no caching of mappings;
 * cache the region if bus traffic ever dominates. */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include "cq_bus.h"

static void die(const char *msg, int err) {
    fprintf(stderr, "cq_runtime: host memory access: %s (%d)\n", msg, err);
    abort();
}

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>

/* Every inaccessible address (unmapped, PROT_NONE, read-only for a write, page 0, kernel,
 * a range past 2^64) comes back as one of these; anything else is not a bad address. */
static int ok(kern_return_t kr) {
    if (kr == KERN_SUCCESS) return 1;
    if (kr != KERN_INVALID_ADDRESS && kr != KERN_PROTECTION_FAILURE && kr != KERN_INVALID_ARGUMENT)
        die("mach_vm call failed", kr);
    return 0;
}

int cq_host_read(uint64_t a, uint8_t *v, uint32_t n) {
    mach_vm_size_t got = 0;
    return ok(mach_vm_read_overwrite(mach_task_self(), a, n, (mach_vm_address_t)(uintptr_t)v, &got)) && got == n;
}

int cq_host_write(uint64_t a, uint8_t v) {
    return ok(mach_vm_write(mach_task_self(), a, (vm_offset_t)&v, 1));
}

#elif defined(__linux__)
/* Not built or tested here (macOS only). process_vm_writev pins pages without FOLL_FORCE, so a
 * read-only page fails with EFAULT like an unmapped one. ENOSYS/EPERM (an old kernel, a seccomp
 * profile) abort rather than silently read 0. */
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

static int xfer(uint64_t a, uint8_t *v, uint32_t n, int write) {
    struct iovec l = { v, n }, r = { (void *)(uintptr_t)a, n };
    ssize_t k = write ? process_vm_writev(getpid(), &l, 1, &r, 1, 0) : process_vm_readv(getpid(), &l, 1, &r, 1, 0);
    if (k == (ssize_t)n) return 1;
    if (k < 0 && errno != EFAULT) die("process_vm_readv/writev failed", errno);
    return 0;   /* EFAULT, or a partial transfer that stopped at a bad page */
}

int cq_host_read(uint64_t a, uint8_t *v, uint32_t n) { return xfer(a, v, n, 0); }
int cq_host_write(uint64_t a, uint8_t v) { return xfer(a, &v, 1, 1); }

#else
#error "src/hostmem.c: no non-faulting host memory access for this platform"
#endif
