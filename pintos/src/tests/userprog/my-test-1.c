/* Try reading a file with size > buffer size.
   This should terminate the process with exit code -1. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char sample[] = {"\"Amazing Electronic Fact\n"};
  int fd = open ("sample.txt");
  read (fd, sample, 239);
  close (fd);
}