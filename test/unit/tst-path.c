/* test_path.c: Tests for xnd/util/path.c */
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "xnd.h"
#include "util/path.h"

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))
#endif

void
test_path_stem (void)
{
  const struct
  {
    const char *path, *exp;
  } inputs[] = { { "/foo/bar.txt", "bar" },
                 { "file.asc", "file" },
                 { "/usr/include/string.h", "string" },
                 { "/etc/ssh/ssh_config", "ssh_config" },
                 { "/dev/urandom", "urandom" },
                 { "/bar/baz/script.sh", "script" } };

  int ntests = ARRAY_SIZE (inputs);
  printf ("Testing xnd_path_stem with %d inputs\n", ntests);

  for (int i = 0; i < ntests; i++)
    {
      char buf[PATH_MAX];
      const char *path = inputs[i].path;
      const char *exp = inputs[i].exp;

      if (xnd_path_stem (buf, PATH_MAX, path) != 0)
        {
          printf ("Test %d failed: "
                  "path=%s, expected=%s, result=%s\n",
                  i, path, exp, "(none)");
          continue;
        }

      if (strncmp (buf, exp, strlen (buf)) != 0)
        {
          printf ("Test %d failed: "
                  "path=%s, expected=%s, result=%s\n",
                  i, path, exp, buf);
          continue;
        }

      printf ("Test %d passed: path=%s, expected=%s, result=%s\n", i, path,
              exp, buf);
    }

  printf ("\n");
}

void
test_path_join (void)
{
  const struct
  {
    const char *l, *r, *exp;
  } inputs[] = {
    { "/dev/", "/zero", "/dev/zero" },     { "/dev", "/null", "/dev/null" },
    { "/etc/", "/hosts", "/etc/hosts" },   { "/dev", "tty", "/dev/tty" },
    { "/usr", "include", "/usr/include" }, { "/usr/", "lib", "/usr/lib" },
  };

  int ntests = ARRAY_SIZE (inputs);
  printf ("Testing xnd_path_join with %d inputs\n", ntests);

  for (int i = 0; i < ntests; i++)
    {
      char buf[PATH_MAX];
      const char *l = inputs[i].l;
      const char *r = inputs[i].r;
      const char *exp = inputs[i].exp;

      if (xnd_path_join (buf, PATH_MAX, l, r) != 0)
        {
          printf ("Test %d failed: "
                  "l=%s, r=%s, expected=%s, result=%s\n",
                  i, l, r, exp, "(none)");
          continue;
        }

      if (strncmp (exp, buf, strlen (buf)) != 0)
        {
          printf ("Test %d failed: "
                  "l=%s, r=%s, expected=%s, result=%s\n",
                  i, l, r, exp, buf);
          continue;
        }

      printf ("Test %d passed: "
              "l=%s, r=%s, expected=%s, result=%s\n",
              i, l, r, exp, buf);
    }

  printf ("\n");
}

int
main (int argc, char *argv[], char *envp[])
{
  test_path_join ();
  test_path_stem ();
}
