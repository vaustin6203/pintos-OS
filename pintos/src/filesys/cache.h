#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>



void bufcache_init(void);

void bufcache_read(block_sector_t sector, void *buffer, 
				   size_t offset, size_t length);

void bufcache_write(block_sector_t sector, void *buffer, 
				   size_t offset, size_t length);

void bufcache_flush(void);
void reset_cache(void);
int get_hit_rate(void);
int get_device_writes(void);