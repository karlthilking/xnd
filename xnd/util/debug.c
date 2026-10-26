/* debug.c */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mach-o/dyld.h>

#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/platform/exe.h"
#include "xnd/util/env.h"
#include "xnd/util/log.h"

struct image_info
{
  uint32_t index;
  const char *name;
  intptr_t slide;
};

static bool image_find (const char *, struct image_info *);

static bool
image_find (const char *target, struct image_info *info)
{
  const char *name = NULL;

  for (uint32_t i = 0; i < _dyld_image_count (); i++)
    {
      name = _dyld_get_image_name (i);
      if (name != NULL && strstr (name, target) != NULL)
        {
          info->index = i;
          info->name = name;
          info->slide = _dyld_get_image_vmaddr_slide (i);
          return true;
        }
    }

  return false;
}

void
dump_debug_info (void)
{
  const char *program = env_get_program_name ();
  struct image_info libxnd, main;

  image_find ("libxnd.dylib", &libxnd);
  image_find (program, &main);

  xnd_log_printf ("Debug info (%d):\n"
                  "libxnd.dylib:\n"
                  " path = %s\n"
                  " index = %u\n"
                  " slide = 0x%lx\n"
                  "%s:\n"
                  " path = %s\n"
                  " index = %u\n"
                  " slide = 0x%lx\n",
                  xnd_sys_getpid (),
                  libxnd.name, libxnd.index, libxnd.slide,
                  program, main.name, main.index, main.slide);
}
