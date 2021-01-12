/* Tests if 'tell' works as it should by printing the position 
   before and after 1 character read. */

#include <stdio.h>
#include <syscall.h>
#include "tests/main.h"

void
test_main (void)
{
  char buf;
  int fd = open ("sample.txt");
  msg("before read current position in file %d", tell(fd));
  read (fd, &buf, 1);
  msg("after 1 char read current position in file %d", tell(fd));
}

