/* Tests the ability of the cache to coalesce writes to the same sector.
We give it a file of ~77KB so the number of device writes should be ~151. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/userprog/sample.inc"

void test_main(void) {
	int size = 128*512;
	int sample_size = sizeof(sample);
	char buffer[sample_size];
	int fd = open("giant.txt");
	CHECK(fd > 1, "open \"giant.txt\"");
	int bytes_written = 0;
	for (int i = 0; i < size; i+=1) {
		bytes_written += write(fd, sample, 1);
	}
	CHECK(bytes_written > size - sample_size, "write \"giant.txt\"");
	int bytes_read = 0;
	seek (fd, 0);
	for (int i = 0; i < bytes_written; i+=1) {
                bytes_read += read(fd, buffer, 1);
        }
	CHECK(bytes_read == bytes_written, "read \"giant.txt\"");
	int device_writes = num_device_writes();
	if(device_writes > bytes_read / 128) {
		msg("number of sectors written to: %d\n", device_writes);
		fail("expected the number of device writes to be less than or equal to %d.", bytes_written/128);
	}
}
