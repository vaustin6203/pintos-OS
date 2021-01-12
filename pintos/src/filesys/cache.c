#include "filesys/cache.h"
#include "filesys/inode.h"
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "devices/block.h"
#include <string.h>
#include "threads/synch.h"
#include <debug.h>

#define NUM_ENTRIES 64
#define INVALID_SECTOR -1

struct data {
	unsigned char contents[BLOCK_SECTOR_SIZE];
};

struct metadata {
	block_sector_t sector;
	struct data *entry;
	struct list_elem lru_elem;
	struct condition until_ready;
	bool ready;
	bool dirty;
};

int num_accesses;
int num_hit;
int wrt_count;

static struct data cached_data[NUM_ENTRIES];

static struct metadata entries[NUM_ENTRIES];

static struct lock cache_lock;

static struct condition until_one_ready;

static struct list lru_list;

struct metadata* bufcache_access(block_sector_t sector, bool blind);

void bufcache_init(void) {
  list_init(&lru_list);
  lock_init(&cache_lock);
  cond_init(&until_one_ready);
  num_hit = 0;
  num_accesses = 0;
  wrt_count = 0;
  for (int i = 0; i < NUM_ENTRIES; i++) {
    entries[i].entry = &cached_data[i];
    entries[i].dirty = false;
    entries[i].ready = true;
    entries[i].sector = INVALID_SECTOR;
    cond_init(&entries[i].until_ready);
    list_push_front(&lru_list, &entries[i].lru_elem);
  }
}

/*Find which entry to remove, picking the one closest to the back of the LRU list*/
static struct metadata* get_eviction_candidate(void) {
	ASSERT(lock_held_by_current_thread(&cache_lock));
	struct metadata *metadata;
	for(struct list_elem *e = list_rbegin(&lru_list); e != list_rend(&lru_list);
		e = list_prev(e)) {
		metadata = list_entry(e, struct metadata, lru_elem);
		if (metadata->ready == true) {
			return metadata;
		}
	}

	return NULL;
}

/*Find an entry in the list of metadata structs*/
static struct metadata* find(block_sector_t sector) {
	ASSERT(lock_held_by_current_thread(&cache_lock));

	for (int i = 0; i < NUM_ENTRIES; i++) {
		if (entries[i].sector == sector) {
			return &entries[i];
		}
	}
	return NULL;
}

/*Write contents of the bufcache entry back to disk*/
static void clean(struct metadata *entry) {
  ASSERT(lock_held_by_current_thread(&cache_lock));
  ASSERT(entry->dirty);
  entry->ready = false;
  lock_release(&cache_lock);
  block_write(fs_device, entry->sector, entry->entry->contents);
  lock_acquire(&cache_lock);
  num_accesses++;
  wrt_count++;
  entry->ready = true;
  entry->dirty = false;
  cond_broadcast(&entry->until_ready, &cache_lock);
  cond_broadcast(&until_one_ready, &cache_lock);
}

/*Replaces an entry in the LRU list*/
static void replace(struct metadata *entry, block_sector_t sector) {
  ASSERT(lock_held_by_current_thread(&cache_lock));
  ASSERT(!(entry->dirty));
  entry->sector = sector;
  entry->ready = false;
  lock_release(&cache_lock);
  block_read(fs_device, entry->sector, entry->entry->contents);
  lock_acquire(&cache_lock);
  num_accesses++;
  entry->ready = true;
  cond_broadcast(&entry->until_ready, &cache_lock);
  cond_broadcast(&until_one_ready, &cache_lock);
}

/*Reads a sector, which it finds by calling bufcache access*/
void bufcache_read(block_sector_t sector, void *buffer,
				   size_t offset, size_t length) {
	ASSERT(offset + length <= BLOCK_SECTOR_SIZE);

	lock_acquire(&cache_lock);
	struct metadata *entry = bufcache_access(sector, false);
	num_accesses++;
	memcpy(buffer, &entry->entry->contents[offset], length);
	lock_release(&cache_lock);
}

/*Scans the bufcache for a specific block, and if it cannot find it, will
  call get_eviction_candidate to make room for a new one, which it pulls
	in from disk. If it can find the block in the bufcache, it will move it
	to the front of the lru list.*/
struct metadata* bufcache_access(block_sector_t sector, bool blind){
	ASSERT(lock_held_by_current_thread(&cache_lock));
	while(1){
		struct metadata* match = find(sector);
		if(match != NULL){
			if(!match->ready){
				cond_wait(&match->until_ready, &cache_lock);
				continue;
			}
			// push to the front of the lru_list
			num_hit++;
			list_remove(&match->lru_elem);
			list_push_front(&lru_list,&match->lru_elem);
			return match;
		}
		struct metadata* to_evict = get_eviction_candidate();
		if(to_evict == NULL){
			cond_wait(&until_one_ready, &cache_lock);
		} else if(to_evict->dirty){
			clean(to_evict);
		} else if(blind){
			// make it to ignore this iteration.
			// on the next iteration, find() should succeed
			to_evict->sector = sector;
		} else {
			replace(to_evict,sector);
			// on the next iteration, find() should succeesd
		}
	}
}

/*Writes into a sector, which it finds the entry for using bufcache access.
  If the entry was not marked dirty before, it is marked dirty after the write.*/
void bufcache_write(block_sector_t sector, void* buffer, size_t offset, size_t length){
	ASSERT(offset+length <= BLOCK_SECTOR_SIZE);
	lock_acquire(&cache_lock);
	struct metadata *entry = bufcache_access(sector, length== BLOCK_SECTOR_SIZE);
	memcpy(&entry->entry->contents[offset], buffer, length);
	num_accesses++;
	entry->dirty = true;
	lock_release(&cache_lock);
}

/*Writes all dirty entries back to disk. Does not clear them out.*/
void bufcache_flush(void){
	lock_acquire(&cache_lock);
	for(int i=0; i < NUM_ENTRIES; i++){
		if(entries[i].dirty){
			clean(&entries[i]);
		}
	}
	lock_release(&cache_lock);
}
int get_hit_rate(void) {
	return (num_hit * 100) / num_accesses;
}

/*Resets the cache by flushing dirty entries and reseting the number of hits and accesses*/
void reset_cache(void) {
	bufcache_flush();
	lock_acquire(&cache_lock);
	num_hit = 0;
	num_accesses = 0;
	wrt_count = 0;
	lock_release(&cache_lock);
}

/*Returns the number of writes that have been made back to disk*/
int get_device_writes(void) {
	return wrt_count;
}
