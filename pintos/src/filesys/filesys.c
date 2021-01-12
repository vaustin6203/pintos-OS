#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);


/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  bufcache_init();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  bufcache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  char *abs_path, *target;
  char buffer[NAME_MAX];
  get_dir(name, &abs_path, &target);
  struct dir *dir = resolve_path(abs_path,buffer);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, target, inode_sector, false));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(abs_path);
  free(target);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char *abs_path, *target;
  char buffer[NAME_MAX];
  get_dir(name, &abs_path, &target);
  struct dir *dir = resolve_path(abs_path,buffer);
  struct inode *inode = NULL;

  if (dir != NULL) {
    if(!dir_lookup (dir, target, &inode)){
      dir_close (dir);
      return NULL;
    }
  }
  dir_close (dir);
  free(abs_path);
  free(target);

  return file_open (inode);
}

/*same as the above function but can open a file or directory */
bool filesys_opendir (const char *name, struct file_struct *filedir)
{
  char *abs_path, *target;
  char buffer[NAME_MAX];
  bool success = false;
  get_dir(name, &abs_path, &target);
  struct dir *dir =  resolve_path(abs_path,buffer);
  struct dir_entry *entry = malloc(sizeof(struct dir_entry));
  if (dir != NULL){ //
      if(strcmp(name,"/") == 0){
        filedir->file_dir = dir_open_root();
        filedir->isdir = true;
        dir_close (dir);
        free(target);
        free(abs_path);
        free(entry);
        return true;

      }
      else if(lookup(dir, target, entry, NULL)){
        struct inode *inode = inode_open(entry->inode_sector);
        filedir->isdir = entry->is_dir;
        filedir->file_dir = entry->is_dir ? (void *) dir_open(inode) : (void *) file_open(inode);
        success = true;
      }
  }
  dir_close (dir);
  free(abs_path);
  free(target);
  free(entry);
  return success;
}




/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  char *abs_path, *target;
  char buffer[NAME_MAX];
  get_dir(name, &abs_path, &target);
  struct dir *dir = resolve_path(abs_path,buffer);
  bool success = dir != NULL && dir_remove (dir, target);
  dir_close (dir);

  return success;
}




/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
