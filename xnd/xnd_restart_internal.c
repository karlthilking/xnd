/* xnd_restart_internal.c */
#include "xnd/xnd.h"
#include "xnd/readckpt.h"
#include "xnd/pac.h"
#include "xnd/vm_region.h"
#include "xnd/shared_cache.h"
#include "xnd/util/io.h"
#include "xnd/pid/pid.h"
#include "xnd/syscall.h"
#include "xnd/platform/ucontext/ucontext.h"

#include <ucontext.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ucontext.h>
#include <sys/mman.h>

#define RESTART_PAUSE_WHILE(expr)                                       \
  if (expr)                                                             \
    {                                                                   \
      printf ("Pausing in %s (pid: %d)\n", __func__, getpid ());        \
      while (expr);                                                     \
    }                                                                   \

static bool restart_pause = false;

/* Location of the temporary restart stack. Kept in (non-constant)
   globals rather than used as immediates so that xrcc's probe build
   and final build generate identical code and segment sizes, whatever
   values the XND_RESTART_* #define's take.  */
mach_vm_address_t xnd_restart_stack_addr = XND_RESTART_STACK_ADDR;
mach_vm_size_t xnd_restart_stack_size = XND_RESTART_STACK_SIZE;

__noreturn noinline void
restart (int fd)
{
  int ret;
  ssize_t bread;
  struct xnd_ckpt_header header;

  if (ckpt_vm_mark_regions () < 0)
    xnd_warn ("Failed to mark restart regions\n");

  bread = sys_readall (fd, &header, sizeof (header));
  if (bread != sizeof (header))
    {
      xnd_error ("Failed to read checkpoint header\n");
      exit (XND_EXIT_FAILURE);
    }

  if (strcmp (header.magic, XND_HEADER_MAGIC))
    {
      xnd_error ("Checkpoint header is invalid\n"
                 "magic: %s, expected: %s\n",
                 header.magic, XND_HEADER_MAGIC);
      exit (XND_EXIT_FAILURE);
    }

  /* For debugging */
  RESTART_PAUSE_WHILE (restart_pause == true);

  ucontext_t uctx;
  enum xnd_ckpt_entry entries[header.entry_count];
  struct xnd_vm_region regions[header.region_count];

  ret = read_ckpt (fd, &header, entries, regions, &uctx);
  if (ret < 0)
    {
      xnd_error ("Failed to read checkpoint file\n");
      exit (XND_EXIT_FAILURE);
    }

  ptrauth_resign_frames ((u64 *) get_ucontext_fp (&uctx));
  xnd_setcontext (&uctx);

  unreachable ();
}

/**
 * jump:
 *  Jump to a temporary stack and initiate the restart.
 */
__noreturn void
jump (int fd)
{
  kern_return_t ret;
  void *sp;
  const mach_vm_size_t size = xnd_restart_stack_size;
  mach_vm_address_t addr = xnd_restart_stack_addr;

  /**
         * Make VM object purgable and associate VM_REGION_RESTART_STACK
         * user_tag with mapping s.t. memory region checkpoint path
         * will know to discard this region.
         */
  ret = mach_vm_map (mach_task_self (), &addr, size, 0,
                     VM_FLAGS_FIXED | VM_FLAGS_PURGABLE
                         | VM_MAKE_TAG (VM_MEMORY_RESTART_STACK),
                     MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_DEFAULT,
                     VM_PROT_ALL, VM_INHERIT_NONE);

  if (ret != KERN_SUCCESS)
    {
      xnd_error ("mach_vm_map: %s\n", mach_error_string (ret));
      exit (XND_EXIT_FAILURE);
    }

  sp = (void *) (addr + size);

  /* Switch to temporary stack and call restart function */
  asm volatile (
      "mov    sp, %[sp]       \n"
      "mov    x0, %[fildes]   \n"
      "blraaz %[restart]      \n"
      :
      : [sp] "r"(sp), [fildes] "r"((long) fd), [restart] "r"(restart));

  unreachable ();
}

__noreturn int
main (int argc, char *argv[])
{
  int fd;
  mach_vm_address_t addr;
  mach_vm_size_t size;
  const char *ckptfile = NULL;
  const struct xnd_error_ops ops =
    {
      .vdprintf = xnd_vdprintf,
      .vsnprintf = xnd_vsnprintf,
    };

  /* restored memory replaces libc state, so only use stack-based
     routines to report errors */
  xnd_error_set_io (&ops);

  for (char **p = argv + 1; *p != NULL; p++)
    {
      if (strncmp (*p, "--pause", sizeof ("--pause")) == 0)
        restart_pause = true;
      else if (strstr (*p, ".xnd") && access (*p, F_OK) == 0)
        ckptfile = *p;
      else
        {
          xnd_error ("Invalid argument: %s\n", *p);
          exit (XND_EXIT_FAILURE);
        }
    }

  if (ckptfile == NULL)
    {
      xnd_error ("No checkpoint file specified\n");
      exit (XND_EXIT_FAILURE);
    }

  /* xnd_restart spawns us with MallocLargeCache=0 so libmalloc never
     allocates its (permanent) vm_reclaim region, which would otherwise
     be placed where the application's memory must be restored */
  addr = ckpt_vm_find_vm_reclaim_region (&size);
  if (addr > 0 && addr < DYLD_SHARED_CACHE_BASE)
    {
      xnd_error ("vm_reclaim region in restore range: 0x%llx-0x%llx "
                 "(was MallocLargeCache=0 ignored?)\n", addr, addr + size);
      exit (XND_EXIT_FAILURE);
    }

  fd = open (ckptfile, O_RDONLY);
  if (fd < 0)
    {
      xnd_perror ("open");
      exit (XND_EXIT_FAILURE);
    }

  xnd_printf ("Restarting from %s (pid=%d)\n", argv[1], getpid ());
  jump (fd);

  unreachable ();
}
