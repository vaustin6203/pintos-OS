/* Tests to see if the hit rate of a cold cache is less than 
 * than a hot one. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {
	int size = 1000;
	char buffer[size];
	int fd = open("design_doc.txt");
	CHECK(fd > 1, "open \"design_doc.txt\"");
	int bytes_read = 0;
	for (int i = 0; i < size; i+=10) {
		bytes_read += read(fd, buffer + i, 10);
	}
	CHECK(bytes_read == size, "read \"design_doc.txt\"");
	int hit1 = hit_rate();
	close(fd);
	fd = open("design_doc.txt");
	CHECK(fd > 1, "open \"design_doc.txt\"");
	bytes_read = 0;
	for (int i = 0; i < size; i+=10) {
                bytes_read += read(fd, buffer + i, 10);
        }
	CHECK(bytes_read == size, "read \"design_doc.txt\"");
    int hit2 = hit_rate();
    close(fd);
	if(hit2 <= hit1) {
		msg("previos hit rate: %d\n", hit1);
		msg("recent hit rate: %d\n", hit2);
		fail("expected the hit rate to improve");
	}
}

