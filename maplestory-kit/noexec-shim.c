/* noexec-shim: LD_PRELOAD library for Wine on a noexec filesystem.
 *
 * Android mounts the SD card noexec. Wine maps PE image sections from the file, so when a
 * program later asks for an executable protection on such a page (e.g. Themida making
 * MapleStory.exe's .rdata PAGE_EXECUTE_READWRITE), the kernel's mprotect() fails with EACCES
 * and the page stays read-only, even though Wine records it as writable. The next write
 * then crashes.
 *
 * When mprotect() fails with EACCES and PROT_EXEC was asked for, this copies the pages into
 * anonymous memory at the same address and retries, which is what Wine already does for
 * executable sections when it first maps an image from a noexec filesystem.
 *
 * Build (no libc headers needed):
 *   clang --target=aarch64-linux-gnu -O2 -fPIC -shared -nostdlib -fuse-ld=lld \
 *     -o noexec-shim.so noexec-shim.c
 */
#include <stddef.h>

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define EACCES 13
#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

#define NR_munmap 215
#define NR_mremap 216
#define NR_mmap 222
#define NR_mprotect 226

extern int* __errno_location(void);

static long sys6(long nr, long a, long b, long c, long d, long e, long f) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  register long x3 __asm__("x3") = d;
  register long x4 __asm__("x4") = e;
  register long x5 __asm__("x5") = f;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
  return x0;
}

static int is_err(long r) {
  return (unsigned long)r > -4096UL;
}

static void copy(char* dst, const char* src, size_t len) {
  /* Plain loop so the compiler does not need memcpy from libc. */
  for (size_t i = 0; i < len; i += sizeof(long)) {
    *(volatile long*)(dst + i) = *(const volatile long*)(src + i);
  }
}

static long move_to_anonymous(void* addr, size_t len, int prot) {
  /* addr and len are page aligned here, because the kernel accepted them before failing. */
  long tmp = sys6(NR_mmap, 0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (is_err(tmp)) {
    return tmp;
  }

  /* The pages may be PROT_NONE; reading them needs PROT_READ, which noexec allows. */
  long r = sys6(NR_mprotect, (long)addr, len, PROT_READ, 0, 0, 0);
  if (!is_err(r)) {
    copy((char*)tmp, addr, len);
    /* mremap replaces the old pages in one step, so other threads never see empty memory. */
    r = sys6(NR_mremap, tmp, len, len, MREMAP_MAYMOVE | MREMAP_FIXED, (long)addr, 0);
    if (!is_err(r)) {
      return sys6(NR_mprotect, (long)addr, len, prot, 0, 0, 0);
    }
  }

  sys6(NR_munmap, tmp, len, 0, 0, 0, 0);
  return r;
}

int mprotect(void* addr, size_t len, int prot) {
  long r = sys6(NR_mprotect, (long)addr, len, prot, 0, 0, 0);
  if (r == -EACCES && (prot & PROT_EXEC) && len) {
    r = move_to_anonymous(addr, len, prot);
  }
  if (is_err(r)) {
    *__errno_location() = (int)-r;
    return -1;
  }
  return 0;
}
