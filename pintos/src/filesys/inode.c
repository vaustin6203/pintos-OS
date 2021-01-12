#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCKS 1
#define INDIRECT_BLOCKS 128
#define DB_INDIRECT_BLOCKS 128*128



void inode_free(block_sector_t inode_sector);
block_sector_t inode_extend(block_sector_t inode_sector, off_t extend_to);

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[122];               /* Not used. */
    uint32_t isdir;
    block_sector_t direct_ptrs;
    block_sector_t singly_indirect_ptrs;
    block_sector_t doubly_indirect_ptrs;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool extending;
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    off_t length;
    struct lock inode_lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns 0 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_sector_t sector = 0;
  size_t index = pos / BLOCK_SECTOR_SIZE;
  if(index < DIRECT_BLOCKS){
    bufcache_read(inode->sector, &sector,offsetof(struct inode_disk, direct_ptrs),
     sizeof(block_sector_t));
  }
  else if (index < DIRECT_BLOCKS + INDIRECT_BLOCKS)
  {
    bufcache_read(inode->sector, &sector, offsetof(struct inode_disk, singly_indirect_ptrs),
     sizeof(block_sector_t));

    bufcache_read(sector, &sector, (index - DIRECT_BLOCKS) * sizeof(block_sector_t),
     sizeof(block_sector_t));
  }
  else if (index < DIRECT_BLOCKS + INDIRECT_BLOCKS + DB_INDIRECT_BLOCKS){
    // level 2 read
    bufcache_read(inode->sector, &sector, offsetof(struct inode_disk, doubly_indirect_ptrs),
     sizeof(block_sector_t));
    // level 1 read
    int double_index = (index - DIRECT_BLOCKS - INDIRECT_BLOCKS) / INDIRECT_BLOCKS;
    bufcache_read(sector, &sector, double_index *sizeof(block_sector_t),
     sizeof(block_sector_t));
    // level 0 read
    int single_index = (index - DIRECT_BLOCKS - INDIRECT_BLOCKS) % INDIRECT_BLOCKS;
    bufcache_read(sector, &sector, single_index * sizeof(block_sector_t),
     sizeof(block_sector_t));

  }
  return sector;
}


/*extends the inode*/
block_sector_t inode_extend(block_sector_t inode_sector, off_t extend_to){

  size_t index = extend_to / BLOCK_SECTOR_SIZE;
  block_sector_t sector , indirect_sector, db_indirect_sector;
  char zeros[BLOCK_SECTOR_SIZE];
  memset(zeros,0,BLOCK_SECTOR_SIZE);

  if(index < DIRECT_BLOCKS){
    off_t direct_offset = offsetof(struct inode_disk, direct_ptrs);
    bufcache_read(inode_sector,&sector, direct_offset, sizeof(block_sector_t));
    if(!sector){
      if(free_map_allocate(1,&sector)){
        bufcache_write(sector,zeros,0,BLOCK_SECTOR_SIZE);
        bufcache_write(inode_sector,&sector,direct_offset, sizeof(block_sector_t) );
      }
      else{
        return 0;
      }
    }
    return sector;
  }
  else if (index < INDIRECT_BLOCKS + DIRECT_BLOCKS){

    bufcache_read(inode_sector,&indirect_sector,
      offsetof(struct inode_disk, singly_indirect_ptrs),
      sizeof(block_sector_t));
    // if indirect blocks haven't been allocated yet.
    if(!indirect_sector){
      if(free_map_allocate(1,&indirect_sector)){
        bufcache_write(indirect_sector,zeros,0,BLOCK_SECTOR_SIZE);

        bufcache_write(inode_sector,&indirect_sector,
          offsetof(struct inode_disk, singly_indirect_ptrs), sizeof(block_sector_t) );
      }
      else{
        return 0;
      }
    }
    // get the index in the second level
    index -= DIRECT_BLOCKS;
    // read second level.
    bufcache_read(indirect_sector, &sector, index*sizeof(block_sector_t),sizeof(block_sector_t));
    /* if not allocated yet set it to zero and write it back
     to the required index in doubly indirect poiters. */
    if(!sector){
      if(free_map_allocate(1,&sector)){
        bufcache_write(sector,zeros,0,BLOCK_SECTOR_SIZE);
        bufcache_write(indirect_sector, &sector, index * sizeof(block_sector_t),
         sizeof(block_sector_t));

      }
      else{
        return 0;
      }
    }
    return sector;

  }
  else if (index < DB_INDIRECT_BLOCKS + INDIRECT_BLOCKS + DIRECT_BLOCKS){
    bufcache_read(inode_sector,&db_indirect_sector,
      offsetof(struct inode_disk, doubly_indirect_ptrs),
      sizeof(block_sector_t));
    // if doubly indirect pointers haven't been allocated yet.
    if(!db_indirect_sector){
      if(free_map_allocate(1,&db_indirect_sector)){
        bufcache_write(db_indirect_sector,zeros,0,BLOCK_SECTOR_SIZE);
        bufcache_write(inode_sector,&db_indirect_sector,
         offsetof(struct inode_disk, doubly_indirect_ptrs),
          sizeof(block_sector_t));

      } else {
        return 0;
      }
    }
    // now that we have second level allocated.
    index -= DIRECT_BLOCKS + INDIRECT_BLOCKS;
    // each db_indirect_ptr has INDIRECT_BLOCks in side so to find index /INDIRECT_BLOCks
    bufcache_read(db_indirect_sector, &indirect_sector,
     (index/INDIRECT_BLOCKS)* sizeof(block_sector_t),
      sizeof(block_sector_t));
    // if the found indirect_sector is not allocated
    if(!indirect_sector){
      if (free_map_allocate(1,&indirect_sector)){
        bufcache_write(indirect_sector,zeros,0,BLOCK_SECTOR_SIZE);
        bufcache_write(db_indirect_sector, &indirect_sector,
         (index/INDIRECT_BLOCKS)* sizeof(block_sector_t),
          sizeof(block_sector_t));

      } else {
        return 0;
      }
    }
    // read the the sector you should read data to.
    bufcache_read(indirect_sector, &sector,
     (index % INDIRECT_BLOCKS) * sizeof(block_sector_t),
     sizeof(block_sector_t));
    /* if the db/ind/dir pointer is not allocated -->
     the correct sector you should read/write data to. */
    if(!sector){
      if(free_map_allocate(1,&sector)){
        bufcache_write(sector,zeros,0,BLOCK_SECTOR_SIZE);
        
        bufcache_write(indirect_sector, &sector,
         (index % INDIRECT_BLOCKS) * sizeof(block_sector_t),
         sizeof(block_sector_t));

      }
      else {
        return 0;
      }
    }
    return sector;
    // end of two level allocation.
  }
  // if it is larger than maximum size return 0;
  else {
    return 0;
  }



// end of the function
}

/*
  ** given a sector for a disk it free up all direct,indirect and db_indirect disk sectors
*/
void inode_free(block_sector_t inode_sector){

  lock_acquire(&free_map_lock);
  block_sector_t sector, i_sector, di_sector;

  /*relase empty and release direct pointer*/
  bufcache_read(inode_sector, &sector,
    offsetof(struct inode_disk, direct_ptrs),
     sizeof(block_sector_t));

  if(sector)
    free_map_release(sector,1);
  sector = 0;
  bufcache_write(inode_sector, &sector,
   offsetof(struct inode_disk, direct_ptrs),
    sizeof(block_sector_t));


  /*release and empty indirect pointer*/
  bufcache_read(inode_sector, &i_sector, offsetof(struct inode_disk, singly_indirect_ptrs), sizeof(block_sector_t));
  if(i_sector){
    for (int i = 0; i < INDIRECT_BLOCKS; ++i){
      /* code */
      bufcache_read(i_sector,&sector,i * sizeof(block_sector_t), sizeof(block_sector_t));
      if (sector){
        /* code */
        free_map_release(sector,1);
      }
      sector = 0;
      bufcache_write(i_sector,&sector,i * sizeof(block_sector_t), sizeof(block_sector_t));
    }
    free_map_release(i_sector,1);
  }
  i_sector = 0;
  bufcache_write(inode_sector, &i_sector, offsetof(struct inode_disk, singly_indirect_ptrs), sizeof(block_sector_t));


  /*release and empty db_indirect pointers*/
  bufcache_read(inode_sector, &di_sector, offsetof(struct inode_disk, doubly_indirect_ptrs), sizeof(block_sector_t));
  if (di_sector){
    // there are INDIRECT_BLOCKS of single_indirect_sectors inside of doubly_indirect_sectores

    /*loop for level 2*/
    for (int i = 0; i < INDIRECT_BLOCKS; ++i){
      /* code */
      bufcache_read(di_sector, &i_sector, i * sizeof(block_sector_t), sizeof(block_sector_t));
      // if the indirect_ptr exists then free everything inside it
      if (i_sector){


        /* loop for level 1 */
        for (int j = 0; i < INDIRECT_BLOCKS; ++i){
          /* code */
          bufcache_read(i_sector, &sector, j * sizeof(block_sector_t), sizeof(block_sector_t));
          // if the entry is not empty then free it.
          if(sector){
            free_map_release(sector,1);
          }
          sector = 0;
          bufcache_write(i_sector, &sector, j * sizeof(block_sector_t), sizeof(block_sector_t));
        } // end of level 1 loop
        // free the indirect_sector
        free_map_release(i_sector,1);
      }

    } // end of level 2 loop.

    // free the di_sector
    free_map_release(di_sector,1);

  } // end of if statement of db_indirect_ptrs
  di_sector = 0;
  
  bufcache_write(inode_sector, &di_sector,
   offsetof(struct inode_disk, doubly_indirect_ptrs),
    sizeof(block_sector_t));

  lock_release(&free_map_lock);
}



/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
struct lock inode_list_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init(&inode_list_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  lock_acquire(&free_map_lock);
  if(!inode_extend(sector,0)){
    lock_release(&free_map_lock);
    inode_free(sector);
    return false;

  }

  lock_release(&free_map_lock);
  bufcache_write(sector, &length, offsetof(struct inode_disk, length), sizeof(off_t));
  unsigned magic_local = INODE_MAGIC;
  
  bufcache_write(sector, &magic_local,
   offsetof(struct inode_disk, magic), sizeof(unsigned) );

  uint32_t is_it = is_dir ? 1 : 0;
  bufcache_write(sector, &is_it, offsetof(struct inode_disk, isdir), sizeof(unsigned) );

  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&inode_list_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release(&inode_list_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    lock_release(&inode_list_lock);
    return NULL;
  }
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init(&inode->inode_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->extending = false;
  lock_release(&inode_list_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  if (inode != NULL)
    inode->open_cnt++;
  lock_release(&inode->inode_lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  lock_acquire(&inode->inode_lock);
  inode->open_cnt--;
  lock_release(&inode->inode_lock);

  /* Release resources if this was the last opener. */
  if (inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire(&inode_list_lock);
      list_remove (&inode->elem);
      lock_release(&inode_list_lock);
      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          inode_free(inode->sector);
          free_map_release (inode->sector, 1);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock_acquire(&inode->inode_lock);
  inode->removed = true;
  lock_release(&inode->inode_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  lock_acquire(&inode->inode_lock);
  block_sector_t sector_idx;
  while (size > 0)
    {
      off_t inode_left = inode_length (inode) - offset;
      /* Disk sector to read, starting byte offset within sector. */
      //block_sector_t sector_idx = byte_to_sector (inode, offset);
      if(inode_left > 0){
        sector_idx = inode_extend(inode->sector, offset);
      } else{
        lock_release(&inode->inode_lock);
        return bytes_read;
        //sector_idx = 0;//byte_to_sector (inode, offset);
      }

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */

      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      bufcache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  lock_release(&inode->inode_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  lock_acquire(&inode->inode_lock);

  if (inode->deny_write_cnt) {
    lock_release(&inode->inode_lock);
    return 0;
  }
  if(size + offset > inode_length(inode)){
    off_t new_lenght = size+offset;
    bufcache_write(inode->sector,&new_lenght, offsetof(struct inode_disk, length), sizeof(off_t));
  }
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */

      block_sector_t sector_idx = inode_extend(inode->sector,offset);
      if (sector_idx == 0)
      {
        lock_release(&inode->inode_lock);
        return bytes_written;
      }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bufcache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_release(&inode->inode_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->inode_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->inode_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;
  bufcache_read(inode->sector,&length, offsetof(struct inode_disk, length), sizeof(off_t));
  return length;
}

uint32_t is_it_dir(block_sector_t sector){
  uint32_t isdir = 0;
  bufcache_read(sector, &isdir, offsetof(struct inode_disk, isdir), sizeof(uint32_t));
  return isdir;
}

bool is_inode_open(block_sector_t sector){
  struct inode *inode;
  struct list_elem *e;
  lock_acquire(&inode_list_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e))
  {
    /* code */
    inode = list_entry(e,struct inode, elem);
    if(inode->sector == sector && inode->open_cnt > 1){
      lock_release(&inode_list_lock);
      return true;
    }
  }
  lock_release(&inode_list_lock);
  return false;
}
