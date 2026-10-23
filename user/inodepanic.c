#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define NFILES 52
#define CHILDREN 4
#define HELD 13

static void
filename(int number, char *name)
{
  name[0] = '/';
  name[1] = 'i';
  name[2] = 'p';
  name[3] = '0' + (number / 10) % 10;
  name[4] = '0' + number % 10;
  name[5] = 0;
}

int
main(void)
{
  int ready[2], release[2];
  int i, j, pid, fd;
  char name[8], status;

  for (i = 0; i < NFILES; i++) {
    filename(i, name);
    fd = open(name, O_CREATE | O_RDWR);
    if (fd < 0) {
      printf("inodepanic: create %s failed\n", name);
      exit(1);
    }
    close(fd);
  }
  printf("inodepanic: created %d files\n", NFILES);

  if (pipe(ready) < 0 || pipe(release) < 0) {
    printf("inodepanic: pipe setup failed\n");
    exit(1);
  }

  for (i = 0; i < CHILDREN; i++) {
    pid = fork();
    if (pid < 0) {
      printf("inodepanic: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      int failed = 0;

      // Keep stdout, ready[1], and release[0]. This leaves 13 slots under
      // NOFILE=16 after descriptors 0 and 2 are closed.
      close(ready[0]);
      close(release[1]);
      close(0);
      close(2);

      for (j = 0; j < HELD; j++) {
        filename(i * HELD + j, name);
        fd = open(name, 0);
        if (fd < 0) {
          printf("inodepanic: child %d open %s failed\n", i, name);
          failed = 1;
          break;
        }
      }

      status = failed ? 'E' : 'R';
      if (write(ready[1], &status, 1) != 1)
        exit(1);
      read(release[0], &status, 1);
      exit(failed);
    }

    if (read(ready[0], &status, 1) != 1) {
      printf("inodepanic: child %d did not report readiness\n", i);
      exit(1);
    }
    if (status == 'E')
      printf("inodepanic: child %d reported expected open failure\n", i);
    else
      printf("inodepanic: child %d holds %d file inodes\n", i, HELD);
  }

  printf("inodepanic: all children reported; releasing them\n");
  for (i = 0; i < CHILDREN; i++)
    if (write(release[1], &status, 1) != 1)
      exit(1);

  close(ready[0]);
  close(ready[1]);
  close(release[0]);
  close(release[1]);
  for (i = 0; i < CHILDREN; i++)
    wait(0);
  printf("inodepanic: completed without kernel panic\n");
  exit(0);
}
