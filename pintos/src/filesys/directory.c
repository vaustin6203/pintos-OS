#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/cache.h"



int get_next_part (char *part, char **srcp);












/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };



/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, block_sector_t parent)
{
  if(inode_create (sector, 0, true)){
    struct inode *inode = inode_open(sector);
    struct dir *dir = dir_open(inode);
    dir_add(dir, ".", sector, true);
    dir_add(dir, "..", parent, true);
    //inode_close(inode);
    dir_close(dir);
    return true;
  }
  return false;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      //inode_reopen(inode);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  if (dir == NULL || dir->inode == NULL) {
    return NULL;
  }
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use && !strcmp (name, e.name))
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool isdir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    return false;
  /* to check the dir that we passed in is removed already so we don't add anything to it*/

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  e.is_dir = isdir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;
  if ( e.is_dir && is_inode_open(e.inode_sector))
  {
    return false;
  }

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
  uint32_t is_dir = is_it_dir(e.inode_sector);
  if(is_dir != 0 && e.in_use){
    struct dir * entry_dir = dir_open(inode_open(e.inode_sector));
    if(get_num_entries(entry_dir) >  2){
      dir_close(entry_dir);
      goto done;
    }
    dir_close(entry_dir);
  }
  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos + (2 * sizeof e)) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use && strcmp(".",name) && strcmp("..",name))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
int get_next_part (char *part, char **srcp) {
  char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/*
given the path separates absolute path and target.
*/
void get_dir(const char *path, char **abs_path, char **target){
  int start_index;
  for(start_index=0; start_index < strlen(path) && path[start_index] == '/'; start_index++);

  int end_index;
  for(end_index = strlen(path) - 1; end_index > start_index && path[end_index] == '/'; end_index--);

  int split_index;
  for(split_index = end_index; split_index >= 0 && path[split_index] != '/'; split_index--);

  *abs_path = malloc(split_index+2); // to include the starting and ending '/';
  *target = malloc(end_index - split_index + 1);
  strlcpy(*abs_path,path, split_index + 2);
  strlcpy(*target,path + split_index + 1, end_index - split_index + 1);
}




/*resolves the path and returns the directory or return NULL if it is not valid*/
struct dir *resolve_path(char *path, char *target){
  int ret_val;
  char *copy_path = malloc(strlen(path)+1);
  char *temp = copy_path;
  strlcpy(copy_path,path,strlen(path)+1);
  struct dir *cwd;
  if (copy_path[0] == '/' || dir_get_inode(thread_current()->cwd) == NULL) {
    cwd = dir_open_root();
  } else {
    cwd = dir_reopen(thread_current()->cwd);
  }
  struct dir_entry *entry = malloc(sizeof(struct dir_entry));
  while((ret_val = get_next_part(target, &copy_path)) != 0){
    if(ret_val == -1){
      dir_close(cwd);
      free(temp);
      free(entry);
      return NULL;
    }
    if(!lookup (cwd, target, entry, NULL) || !entry->is_dir){
      dir_close(cwd);
      free(temp);
      free(entry);
      return NULL;
    }
    dir_close(cwd);
    struct inode *inode = inode_open(entry->inode_sector);
    cwd = dir_open(inode);
    //inode_close(inode);
  }
  free(temp);
  free(entry);
  return cwd;
}

/* given a dir it will return the associated inode_number*/
block_sector_t dir_inode_number(struct dir *dir){
  return inode_get_inumber(dir->inode);
}



/* Scans the directory to check how many entries are present. Used to see
   if the directory is empty*/
int get_num_entries(struct dir * dir) {
  int i = 0;
  struct dir_entry e;
  off_t ofs;
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) {
         if (!e.in_use)
           break;
         i++;
  }
  return i;
}
