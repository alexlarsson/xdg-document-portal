#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixinputstream.h>

#include "xdp-dbus.h"

extern int do_cat (int argc, char *argv[]);
extern int do_add (int argc, char *argv[]);
extern int do_new (int argc, char *argv[]);
extern int do_update (int argc, char *argv[]);

static void
usage (void)
{
  g_printerr ("Usage: xdp COMMAND [ARGUMENTS...]\n");
  exit (1);
}

int
main (int argc, char *argv[])
{
  char *command;

  setlocale (LC_ALL, "");
  g_set_prgname (argv[0]);

  if (argc < 2)
    usage ();

  command = argv[1];
  argc -= 2;
  argv += 2;

  if (strcmp (command, "cat") == 0)
    return do_cat (argc, argv);
  else if (strcmp (command, "add") == 0)
    return do_add (argc, argv);
  else if (strcmp (command, "new") == 0)
    return do_new (argc, argv);
  else if (strcmp (command, "update") == 0)
    return do_update (argc, argv);
  else
    usage ();

  return 0;
}
