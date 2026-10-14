/* xnd_restart_cc.c

   Compile xnd_restart_internal with the correct segment addresses
   and segment sizes required to prevent the restart binary's memory
   from interfering with saved userspace memory during restore.

   xnd_restart_internal also needs to be compiled #define's indicating
   where it should map its restart stack, how large the restart stack
   should be, etc.

   xnd_restart_internal should start at virtual address 0x500000000000,
   and extend upwards to provide enough room for its __TEXT, __DATA,
   __DATA_CONST, and __LINKEDIT segments

   dyld with map itself above the main executable (xnd_restart_internal)
   so we reserve 100 pages (16kb each) between the high address of
   xnd_restart_internal and where it should map its restart stack.

   restart #defines (14):
    - XND_RESTART_TEXT_ADDR + XND_RESTART_TEXT_SIZE
    - XND_RESTART_DATA_CONST + XND_RESTART_DATA_CONST_SIZE
    - XND_RESTART_DATA_ADDR + XND_RESTART_DATA_SIZE
    - XND_RESTART_LINKEDIT_ADDR + XND_RESTART_LINKEDIT_SIZE
    - XND_RESTART_DYLD_ADDR + XND_RESTART_DYLD_SIZE
    - XND_RESTART_STACK_ADDR + XND_RESTART_STACK_SIZE
    - XND_GUARD_ADDR + XND_GUARD_SIZE (for vm_reclaim buffer)

   linker flags (5):
    - -Wl,-segaddr,__TEXT,<text-address>
    - -Wl,-segaddr,__DATA,<data-address>
    - -Wl,-segaddr,__DATA_CONST,<data-const-address>
    - -Wl,-segaddr,__LINKEDIT,<linkedit-address>
    - -Wl, -ldclassic
 */

#include "common/compiler.h"
#include "common/xalloc.h"
#include "common/xunistd.h"
#include "common/byte_order.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach/mach_vm.h>
#include <mach/vm_page_size.h>
#include <mach-o/loader.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define XND_RESTART_CC "xnd_restart_cc"

#define ANSI_RED "\033[0;31m"
#define ANSI_RESET "\033[0m"

#define panic(fmt, ...)                         \
  do {                                          \
    dprintf (2, "%s: " ANSI_RED fmt ANSI_RESET, \
             XND_RESTART_CC, ##__VA_ARGS__);    \
    exit (1);                                   \
  } while (0);

#ifndef trunc_page
# define trunc_page(x) ((x) & ~vm_page_mask)
#endif

#ifndef round_page
# define round_page(x) trunc_page ((x) + vm_page_mask)
#endif

/* default parameters to use when compiling xnd_restart_internal,
   allow for overriding with -D<name>=value when compiling
   xnd_restart_cc itself */
#ifndef XND_RESTART_BASE_ADDR
# define XND_RESTART_BASE_ADDR 0x0000500000000000ULL
#endif
#ifndef XND_RESTART_DYLD_SIZE
# define XND_RESTART_DYLD_SIZE 0x0000000000190000ULL
#endif
#ifndef XND_RESTART_STACK_SIZE
# define XND_RESTART_STACK_SIZE 0x0000000000100000ULL
#endif
#ifndef CC
# define CC "clang"
#endif

/* parameters for xnd_restart_internal's fabricated guard page, used
   for relocating the kernel-mapped deferred reclaim buffer
   (also overridable with -DXND_RESTART_GUARD_<name>=<value>) */
#ifndef XND_RESTART_GUARD_ADDR
# define XND_RESTART_GUARD_ADDR 0x0000000100000000ULL
#endif
#ifndef XND_RESTART_GUARD_SIZE
# define XND_RESTART_GUARD_SIZE 0x000000007b500000ULL
#endif
#ifndef XND_RESTART_GUARD_SEGNAME
# define XND_RESTART_GUARD_SEGNAME "__XND"
#endif
#ifndef XND_RESTART_GUARD_SECTNAME
# define XND_RESTART_GUARD_SECTNAME "__xnd"
#endif
#ifndef XND_RESTART_GUARD_PROT
# define XND_RESTART_GUARD_PROT "---" /* VM_PROT_NONE */
#endif
#ifndef XND_RESTART_GUARD_MAXPROT
# define XND_RESTART_GUARD_MAXPROT "rwx" /* VM_PROT_ALL */
#endif

/* number of command-line arguments passed to segpatch when patching
   xnd_restart_internal (not including the path of segpatch itself):

   ./segpatch xnd_restart_internal --vmaddr <addr> --vmsize <size>
   --segname <segname> --sectname <sectname> --prot <prot>/<maxprot> */
#define SEGPATCH_NARGS 11

/* number of #define's and linker flags used when compiling
   xnd_restart_internal */
#define NDEFS 14

/* disable -Wl,-ld_classic by default, assuming that the newer linker
   will support and honor our -segaddr linker flags */
#ifndef USE_LD_CLASSIC
# define USE_LD_CLASSIC 0
#endif
#if USE_LD_CLASSIC
# define NLDFLAGS 5
#else
# define NLDFLAGS 4
#endif

struct restart_params
{
  uint64_t text_addr;
  uint64_t text_size;

  uint64_t data_const_addr;
  uint64_t data_const_size;

  uint64_t data_addr;
  uint64_t data_size;

  uint64_t linkedit_addr;
  uint64_t linkedit_size;

  uint64_t dyld_addr;
  uint64_t dyld_size;

  uint64_t stack_addr;
  uint64_t stack_size;

  uint64_t guard_addr;
  uint64_t guard_size;

  char *defs[NDEFS];
  char *ldflags[NLDFLAGS];
};

#define def_fmt "-D%s=0x%016llXULL"
#define alloc_def_flag(macro, value)                            \
  ({                                                            \
    char *ptr;                                                  \
    xasprintf (&ptr, def_fmt, macro, value);                    \
    ptr;                                                        \
  })

#define segaddr_fmt "-Wl,-segaddr,%s,0x%016llX"
#define alloc_segaddr_flag(segname, addr)               \
  ({                                                    \
    char *ptr;                                          \
    assert (segname != NULL);                           \
    xasprintf (&ptr, segaddr_fmt, segname, addr);       \
    ptr;                                                \
  })

struct macho_segments
{
  struct mach_header_64 mh;
  struct segment_command_64 text;
  struct segment_command_64 data_const;
  struct segment_command_64 data;
  struct segment_command_64 linkedit;
};

struct argvec
{
  const char **list;
  size_t len;
  size_t size;
};

static void argvec_init (struct argvec *, size_t);
static void argvec_free (struct argvec *);
static void argvec_resize (struct argvec *, size_t);
static void argvec_push (struct argvec *, const char *);
static void argvec_pushn (struct argvec *, size_t, ...);
static void argvec_emplace (struct argvec *, const char *const *, size_t);
static void argvec_concat (struct argvec *, const struct argvec *);
static void argvec_concatn (struct argvec *, size_t, ...) __unused;

static void restart_params_init (struct restart_params *,
                                 struct macho_segments *);
static void restart_params_free (struct restart_params *);

static void macho_segments_init (struct macho_segments *, const char *);
static int subprocess_run (char *const *);

static void xrcompile (struct argvec *);
static void xrpatch (const char *, struct restart_params *) __unused;

static void usage (void);

/* meaningless #define's so we can compile xnd_restart_internal once
   before determine segment addresses and sizes without 'undeclared
   identifier' compiler errors */
static const char *empty_defs[] =
  {
    "-DXND_RESTART_TEXT_ADDR=0ULL",
    "-DXND_RESTART_TEXT_SIZE=0ULL",
    "-DXND_RESTART_DATA_CONST_ADDR=0ULL",
    "-DXND_RESTART_DATA_CONST_SIZE=0ULL",
    "-DXND_RESTART_DATA_ADDR=0ULL",
    "-DXND_RESTART_DATA_SIZE=0ULL",
    "-DXND_RESTART_LINKEDIT_ADDR=0ULL",
    "-DXND_RESTART_LINKEDIT_SIZE=0ULL",
    "-DXND_RESTART_DYLD_ADDR=0ULL",
    "-DXND_RESTART_DYLD_SIZE=0ULL",
    "-DXND_RESTART_STACK_ADDR=0ULL",
    "-DXND_RESTART_STACK_SIZE=0ULL",
    "-DXND_RESTART_GUARD_ADDR=0ULL",
    "-DXND_RESTART_GUARD_SIZE=0ULL"
  };
_Static_assert (countof (empty_defs) == NDEFS,
                "xnd_restart_cc: unexpected number of empty #define's");

static const char *macronames[14] =
  {
    "XND_RESTART_TEXT_ADDR",
    "XND_RESTART_TEXT_SIZE",
    "XND_RESTART_DATA_CONST_ADDR",
    "XND_RESTART_DATA_CONST_SIZE",
    "XND_RESTART_DATA_ADDR",
    "XND_RESTART_DATA_SIZE",
    "XND_RESTART_LINKEDIT_ADDR",
    "XND_RESTART_LINKEDIT_SIZE",
    "XND_RESTART_DYLD_ADDR",
    "XND_RESTART_DYLD_SIZE",
    "XND_RESTART_STACK_ADDR",
    "XND_RESTART_STACK_SIZE",
    "XND_RESTART_GUARD_ADDR",
    "XND_RESTART_GUARD_SIZE"
  };
_Static_assert (countof (macronames) == NDEFS,
                "xnd_restart_cc: unexpected number of macro names");

static void
argvec_init (struct argvec *vec, size_t size)
{
  const size_t least = 8;

  vec->len = 0;
  vec->size = max (least, ((size + 7) & ~7));
  vec->list = xmalloc (vec->size * sizeof (char *));
}

static void
argvec_free (struct argvec *vec)
{
  vec->len = 0;
  vec->size = 0;
  free (exchange (vec->list, NULL));
}

static void
argvec_resize (struct argvec *vec, size_t size)
{
  void *ptr = (void *) vec->list;

  vec->size = size;
  vec->list = xrealloc (ptr, vec->size * sizeof (char *));
}

static void
argvec_push (struct argvec *vec, const char *arg)
{
  if (vec->len >= vec->size)
    argvec_resize (vec, vec->size * 2);

  vec->list[vec->len++] = arg;
}

static void
argvec_pushn (struct argvec *vec, size_t n, ...)
{
  va_list ap;
  const char *arg;

  va_start (ap, n);
  for (size_t i = 0; i < n; i++)
    {
      arg = va_arg (ap, const char *);
      argvec_push (vec, arg);
    }
  va_end (ap);
}

static void
argvec_emplace (struct argvec *vec, const char *const *list, size_t len)
{
  size_t size;
  const size_t n = vec->len + len;

  if (n > vec->size)
    {
      size = max (vec->size * 2, ((n + 7) & ~7));
      argvec_resize (vec, size);
    }

  for (size_t i = 0; i < len; i++)
    *(vec->list + vec->len++) = *(list + i);
}

static void
argvec_concat (struct argvec *dst, const struct argvec *src)
{
  size_t size;
  const size_t n = dst->len + src->len;

  if (n > dst->size)
    {
      size = max (dst->size * 2, ((n + 7) & ~7));
      argvec_resize (dst, size);
    }

  for (size_t i = 0; i < src->len; i++)
    *(dst->list + dst->len++) = *(src->list + i);
}

static void
argvec_concatn (struct argvec *dst, size_t n, ...)
{
  va_list ap;
  const struct argvec *src;

  va_start (ap, n);
  for (size_t i = 0; i < n; i++)
    {
      src = va_arg (ap, const struct argvec *);
      argvec_concat (dst, src);
    }
  va_end (ap);
}

static void
restart_params_init (struct restart_params *params,
                     struct macho_segments *segments)
{
  uint64_t addr, size;
  size_t dcnt = 0, lcnt = 0;

  /* Set all segment address and size parameters, and construct
     #define's for each parameter. For base address parameters for
     text, const data, data, or linkedit segments, also construct a
     linker flag (-Wl,-segaddr,<name>,<address>) to be used when
     compiling xnd_restart_internal. */

#define set_segaddr_param(field, addr, segname)                 \
  do {                                                          \
    char *__flag;                                               \
    char *__seg = (char *) (segname);                           \
    params->field = addr;                                       \
    __flag = alloc_def_flag (macronames[dcnt], addr);           \
    params->defs[dcnt++] = __flag;                              \
    if (__seg != NULL)                                          \
      {                                                         \
        __flag = alloc_segaddr_flag (__seg, (addr));            \
        params->ldflags[lcnt++] = __flag;                       \
      }                                                         \
  } while (0)
    
#define set_segsize_param(field, size)                          \
  do {                                                          \
    char *__flag;                                               \
    params->field = size;                                       \
    __flag = alloc_def_flag (macronames[dcnt], size);           \
    params->defs[dcnt++] = __flag;                              \
  } while (0)

  /* 1. __TEXT address */
  addr = XND_RESTART_BASE_ADDR;
  set_segaddr_param (text_addr, addr, "__TEXT");

  /* 2. __TEXT size */
  size = round_page (segments->text.vmsize);
  set_segsize_param (text_size, size);

  /* 3. __DATA_CONST address */
  addr += size;
  set_segaddr_param (data_const_addr, addr, "__DATA_CONST");
  
  /* 4. __DATA_CONST size */
  size = round_page (segments->data_const.vmsize);
  set_segsize_param (data_const_size, size);
  
  /* 5. __DATA address */
  addr += size;
  set_segaddr_param (data_addr, addr, "__DATA");
  
  /* 6. __DATA size */
  size = round_page (segments->data.vmsize);
  set_segsize_param (data_size, size);

  /* 7. __LINKEDIT address */
  addr += size;
  set_segaddr_param (linkedit_addr, addr, "__LINKEDIT");

  /* 8. __LINKEDIT size */
  size = round_page (segments->linkedit.vmsize);
  set_segsize_param (linkedit_size, size);

  /* 9. dyld address */
  addr += size;
  set_segaddr_param (dyld_addr, addr, NULL);

  /* 10. dyld size */
  size = round_page (XND_RESTART_DYLD_SIZE);
  set_segsize_param (dyld_size, size);

  /* 11. restart stack address */
  addr += size;
  set_segaddr_param (stack_addr, addr, NULL);

  /* 12. restart stack size */
  size = round_page (XND_RESTART_STACK_SIZE);
  set_segsize_param (stack_size, size);

  /* 13. restart guard address */
  addr = round_page (XND_RESTART_GUARD_ADDR);
  set_segaddr_param (guard_addr, addr, NULL);

  /* 14. restart guard size */
  size = round_page (XND_RESTART_GUARD_SIZE);
  set_segsize_param (guard_size, size);
  
  /* final ldflag to make sure -Wl,-segaddr is recognized by the
     linker if we are on a newer version */
#if USE_LD_CLASSIC
  params->ldflags[lcnt++] = xstrdup ("-Wl,-ld_classic");
#endif
  assert (lcnt == NLDFLAGS && dcnt == NDEFS);

#undef set_segaddr_param
#undef set_segsize_param
}

static void
restart_params_free (struct restart_params *params)
{
  for (size_t i = 0; i < NDEFS; i++)
    free (params->defs[i]);
  for (size_t i = 0; i < NLDFLAGS; i++)
    free (params->ldflags[i]);
}

static void
macho_segments_init (struct macho_segments *segments, const char *path)
{
  void *addr;
  size_t len;
  uintptr_t base, offset = 0;
  struct mach_header_64 *mh;
  struct load_command *lc;
  struct segment_command_64 *seg;
  struct segment_command_64 *text = NULL;
  struct segment_command_64 *data_const = NULL;
  struct segment_command_64 *data = NULL;
  struct segment_command_64 *linkedit = NULL;

  {
    int fd;
    struct stat st;

    fd = xopen (path, O_RDONLY);
    xfstat (fd, &st);
    
    len = round_page (st.st_size);
    addr = xmmap (NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    xclose (fd);
  }

  mh = (struct mach_header_64 *) addr;
  /* mach header should be 64-bit and should not be in big endian
     byte order */
  assert (mh->magic == MH_MAGIC_64);

  /* load command start after mach header */
  base = (uintptr_t) addr + sizeof (struct mach_header_64);
  do
    {
      lc = (struct load_command *) (base + offset);
      /* expecting LC_SEGMENT_64 only */
      assert (lc->cmd != LC_SEGMENT);
      if (lc->cmd != LC_SEGMENT_64)
        continue;

      seg = (struct segment_command_64 *) lc;
      if (strcmp (seg->segname, "__TEXT") == 0)
        text = seg;
      else if (strcmp (seg->segname, "__DATA_CONST") == 0)
        data_const = seg;
      else if (strcmp (seg->segname, "__DATA") == 0)
        data = seg;
      else if (strcmp (seg->segname, "__LINKEDIT") == 0)
        linkedit = seg;
    }
  while ((offset += lc->cmdsize) < mh->sizeofcmds);

  if (text == NULL || data_const == NULL
      || data == NULL || linkedit == NULL)
    {
      fprintf (stderr, "failed to find all segments in %s\n", path);
      exit (1);
    }

  memcpy (&segments->mh, mh, sizeof (*mh));
  memcpy (&segments->text, text, sizeof (*text));
  memcpy (&segments->data_const, data_const, sizeof (*data_const));
  memcpy (&segments->data, data, sizeof (*data));
  memcpy (&segments->linkedit, linkedit, sizeof (*linkedit));

  xmunmap (addr, len);
}

static int
subprocess_run (char *const *argv)
{
  pid_t pid;
  int killed, status, exit_status;

  pid = xfork ();
  if (pid == 0)
    {
      xexecvp (argv[0], argv);
      unreachable ();
    }

  for (;;)
    {
      killed = waitpid (pid, &status, 0);
      if (killed == pid)
        break;
      else if (killed < 0 && errno != EINTR)
        {
          perror ("waitpid");
          exit (1);
        }
    }

  if (WIFEXITED (status))
    exit_status = WEXITSTATUS (status);
  else
    {
      assert (WIFSIGNALED (status));
      exit_status = 128 + WTERMSIG (status);
    }

  return exit_status;
}

static void
xrcompile (struct argvec *options)
{
  int exit_status;
  char **args;
  size_t nargs = options->len + 1;

  args = xmalloc ((nargs + 1) * sizeof (*args));
  args[0] = CC;
  args[nargs] = NULL;

  for (size_t i = 1; i < nargs; i++)
    *(args + i) = (char *) *(options->list + i - 1);

  exit_status = subprocess_run ((char *const *) args);
  if (exit_status != 0)
    {
      fprintf (stderr, "%s exited %d\n", CC, exit_status);
      exit (1);
    }

  free (args);
}

static void
xrpatch (const char *out, struct restart_params *params)
{
  int exit_status;
  char prot[8], segpatch[PATH_MAX];
  char *addr, *size;

  /* Find path to segpatch; BUILD=<value> must be specified in the
     environment so we know what directory segpatch will be located in */
  {
    char *build = getenv ("BUILD");
    if (build == NULL)
      panic ("BUILD not in environment\n");

    strlcpy (segpatch, build, sizeof (segpatch));
    strlcat (segpatch, "/", sizeof (segpatch));
    strlcat (segpatch, "segpatch", sizeof (segpatch));

    if (access (segpatch, X_OK) != 0)
      panic ("segpatch not found: %s\n", segpatch);
  }

  xasprintf (&addr, "0x%llX", params->guard_addr);
  xasprintf (&size, "0x%llX", params->guard_size);

  /* vm protection string: <prot>/<maxprot> */
  strlcpy (prot, XND_RESTART_GUARD_PROT, sizeof (prot));
  strlcat (prot, "/", sizeof (prot));
  strlcat (prot, XND_RESTART_GUARD_MAXPROT, sizeof (prot));

  /* ./segpatch xnd_restart_internal --vmaddr <addr> --vmsize <size>
     --segname <segname> --sectname <sectname> --prot <prot>/<maxprot> */
  const char *const args[] =
    {
      segpatch, out,
      "--vmaddr", addr,
      "--vmsize", size,
      "--segname", XND_RESTART_GUARD_SEGNAME,
      "--sectname", XND_RESTART_GUARD_SECTNAME,
      "--prot", prot, NULL,
    };

  exit_status = subprocess_run ((char *const *) args);
  if (exit_status != 0)
    {
      fprintf (stderr, "%s exited %d\n", segpatch, exit_status);
      exit (1);
    }

  free (addr);
  free (size);
}

static void
usage (void)
{
  fprintf (stderr, "Usage: %s [options] file ...\n", XND_RESTART_CC);
}

int
main (int argc, char *argv[])
{
  size_t len;
  const char *path = "a.out";
  struct argvec cc_args, cflags, srcs;
  struct macho_segments segments = {0};
  struct restart_params params = {0};

  if (argc < 2)
    {
      usage ();
      exit (1);
    }

  argvec_init (&cflags, 8);
  argvec_init (&srcs, 8);

#define shift *(argc--, argv++)
  shift;
  while (argc != 0)
    {
      if (strncmp (*argv, "-iquote", strlen ("-iquote")) == 0
          || strncmp (*argv, "-isystem", strlen ("-isystem")) == 0
          || strncmp (*argv, "-include", strlen ("-include")) == 0)
        {
          argvec_push (&cflags, shift);
          argvec_push (&cflags, shift);
        }
      else if (strncmp (*argv, "-o", strlen ("-o")) == 0)
        {
          shift;
          path = shift;
        }
      else if (access (*argv, R_OK) == 0)
        argvec_push (&srcs, shift);
      else
        argvec_push (&cflags, shift);
    }

  if (srcs.len < 1)
    panic ("no inputs files\n");

  len = cflags.len + NDEFS + srcs.len + 2;
  argvec_init (&cc_args, len);

  argvec_concat (&cc_args, &cflags);
  argvec_emplace (&cc_args, (const char *const *)empty_defs, NDEFS);
  argvec_concat (&cc_args, &srcs);
  argvec_pushn (&cc_args, 2, "-o", path);

  /* compile xnd_restart_internal without the #define's and linker flags
     so we can obtain the LC_SEGMENT_64 load commands for
     xnd_restart_internal's text, const data, data, and linkedit
     segments */
  xrcompile (&cc_args);
  argvec_free (&cc_args);

  /* obtain segment sizes from LC_SEGMENT_64 load command in output
     executable and remove temporary executable */
  macho_segments_init (&segments, path);
  xunlink (path);

  /* obtain xnd_restart_internal parameters (#define's and ld flags)
     inferred from the segment sizes found from compiling without
     and linker flags */
  restart_params_init (&params, &segments);
  len = cflags.len + NDEFS + NLDFLAGS + srcs.len + 2;
  argvec_init (&cc_args, len);

  /* build final compilation flags
     [cflags] [#define's] [ldflags] [sources] -o file */
  argvec_concat (&cc_args, &cflags);
  argvec_emplace (&cc_args, (const char *const *)params.defs, NDEFS);
  argvec_emplace (&cc_args, (const char *const *)params.ldflags, NLDFLAGS);
  argvec_concat (&cc_args, &srcs);
  argvec_pushn (&cc_args, 2, "-o", path);

  xrcompile (&cc_args);
  argvec_free (&cc_args);
  argvec_free (&cflags);
  argvec_free (&srcs);

  /* invoke segpatch on xnd_restart_internal to insert our guard region
     in xnd_restart_internal (only if the deferred reclaim buffer will
     be present, otherwise this can be skipped) */
#if XND_DEFERRED_RECLAIM_ENABLED
  xrpatch (path, &params);
#endif

  /* print #define's to stdout (captured by the Makefile so they can
     be reused when compiling other objects interested in
     xnd_restart_internal's segment locations */
  for (size_t i = 0; i < NDEFS; i++)
    printf ((i + 1 < NDEFS) ? "%s " : "%s\n", params.defs[i]);
  
  restart_params_free (&params);
  exit (0);
}
