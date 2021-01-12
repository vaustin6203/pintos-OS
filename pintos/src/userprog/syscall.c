#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "userprog/pagedir.h"
#include "filesys/directory.h"
#include "filesys/cache.h"

static void syscall_handler (struct intr_frame *);
int open_helper(struct thread *t);
static int validate_address (void *addr);
static void validate_args (void *esp, int argc);
void syscall_helper(struct intr_frame *f);
int read_stdin(char *buffer, int32_t size);
int sys_inumber_helper(int fd,struct thread *t);
int sys_open_helper(const char *name, struct thread *t);
int read_helper(int fd, char *buffer, unsigned size, struct thread *t);
void help_close_files(struct thread *cur);
bool sys_remove_helper(char *name);
bool sys_idir_helper(int fd,struct thread *t);
bool sys_readdir_helper(int fd, char *name, struct thread *t);
bool sys_chdir_helper(const char *name);
bool sys_mkdir_helper(const char *name);
int close_helper(int fd, struct thread *t);
int sys_write_helper(int fd,const char *buffer,int32_t size,struct thread *t);
uint32_t sys_read_helper(int fd, char *buffer, int32_t size);





/* Traverses thread's file descriptor list to check for an FD. */
struct file_struct *file_found(int fd, struct thread * t) {
  struct list_elem *e;
  for (e = list_begin(&(t->file_list)); e != list_end(&(t->file_list)); e = list_next(e)) {
    struct file_struct *entry = list_entry(e, struct file_struct, elem);
    if (entry->fd == fd) {// could check isdir bool to make sure if it is a file
      return entry;
    }
  }
  return NULL;
}

/*Helps find the inumber of a file or directory's inode, given the file descriptor*/
int sys_inumber_helper(int fd,struct thread *t){
  struct file_struct *file= file_found(fd,t);
  if(file == NULL){
    return -1;
  }
  else {
    if (file->isdir)
    {
      return dir_inode_number((struct dir *) file->file_dir);
    }else{
      return file_inode_number((struct file *) file->file_dir);
    }
  }
}

/*Helper function for SYS_REMOVE, used because all other syscalls have a helper*/
bool sys_remove_helper(char *name){
   return  filesys_remove(name);
}

/*Checks if a file is a directory or not in the ISDIR syscall*/
bool sys_idir_helper(int fd,struct thread *t){
  struct file_struct *file= file_found(fd,t);
  if(file == NULL){
    sys_helper_exit(-1);
  }
  return file->isdir;
}

/*Helps read the directory in the readdir syscall*/
bool sys_readdir_helper(int fd, char *name, struct thread *t){
  if(name == NULL){
    return false;
  }
  struct file_struct *file= file_found(fd,t);
  if (file == NULL || !file->isdir){
    return false;
  }
  return dir_readdir((struct dir *) file->file_dir, name);
}

/*Helps change the directory in the chdir syscall*/
bool sys_chdir_helper(const char *name){
  if(name == NULL || name[0] =='\0'){
    return false;
  }
  struct thread *t = thread_current();
  int fd = sys_open_helper(name, t);
  if(fd == -1){
    return false;
  }
  struct file_struct *dir_fs = file_found(fd, t);
  struct dir *dir = (struct dir *)dir_fs->file_dir;
  if (dir_inode_number(dir) == dir_inode_number(t->cwd)) {
    list_remove(&dir_fs->elem);
    t->openfds[dir_fs->fd] = true;
    dir_close(dir);
    free(dir_fs);
    return true;
  }
  list_remove(&dir_fs->elem);
  free(dir_fs);
  dir_close(t->cwd);
  t->cwd = dir;
  return true;
}


/*Helps make a directory in the mkdir syscall*/
bool sys_mkdir_helper(const char *name){
  if(name == NULL || name[0] =='\0'){
    return false;
  }
  block_sector_t inode_sector = 0;
  char *abs_path, *target;
  char buffer[NAME_MAX];
  get_dir(name, &abs_path, &target);
  struct dir *dir = resolve_path(abs_path,buffer);
  bool success = false; //
  if(dir != NULL){
    if(free_map_allocate (1, &inode_sector)
      &&dir_create (inode_sector, dir_inode_number(dir))
      && dir_add (dir, target, inode_sector, true) ){
      success = true;
    }
  }
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  free(abs_path);
  free(target);
  dir_close(dir);
  return success;
}




/* Closes all files in a thread's file descriptor list.
   Used for exiting a thread. */
void help_close_files(struct thread *cur){
  struct file_struct *entry;
  struct list_elem *e;
  while (!list_empty(&(cur->file_list))) {
    e = list_pop_front (&(cur->file_list));
    entry = list_entry(e, struct file_struct, elem);
    if(entry->isdir){
      dir_close((struct dir *) entry->file_dir);
    } else {
      file_close((struct file *) entry->file_dir);
    }
    cur->openfds[entry->fd] = true;
    free(entry);

  }
}

/*Helps open a file or directory based on whether it is a directory*/
int sys_open_helper(const char *name, struct thread *t){
  if(name == NULL){
    return -1;
  }
  struct file_struct *filestruct = malloc(sizeof(struct file_struct));
  bool success = filesys_opendir(name, filestruct);
  if(!success){
      free(filestruct);
      return -1;
  }
  filestruct->fd = open_helper(t);
  if(filestruct->fd == -1){
    if(filestruct->isdir)
      file_close((struct file *) filestruct->file_dir);
    else
      dir_close((struct dir *)filestruct->file_dir);
    free(filestruct);
    return -1;
  }
  list_push_front(&(t->file_list), &(filestruct->elem));
  return filestruct->fd;
}

/* Finds the first open slot in open file descriptions array
   to open a new file. */
int open_helper(struct thread *t){
  for (int i = 3; i < MAX_FD; ++i)
  {
    if(t->openfds[i]){
      t->openfds[i] = false;
      return i;
    }
  }
  return -1;
}


/* Given file descriptor, closes one file in thread's file descriptor list.
   Used for closing a single file within a thread process. */
int close_helper(int fd, struct thread *t) {
  if (fd < 3 || fd >= MAX_FD){
    sys_helper_exit(-1);
  }
  struct file_struct *entry = file_found(fd, t);
  if(entry == NULL){
    return -1;
  }
  else
  {
    if(entry->isdir){
      dir_close((struct dir *) entry->file_dir);
    }
    else
    {
      file_close((struct file *) entry->file_dir);
    }
    list_remove(&entry->elem);
    t->openfds[fd] = true;
    free(entry);
    return 0;
  }
}

/* Writes buffer to stdout. */
int write_stdout(const char *buffer, int32_t size) {
  int num_bytes_to_write = (int) size;
  while (num_bytes_to_write >= 200) {
    putbuf(buffer, 200);
    buffer += 200;
    num_bytes_to_write -= 200;
  }
  putbuf(buffer, num_bytes_to_write);
  return (int) size;
}


/* Helps write a file*/
int sys_write_helper(int fd,const char *buffer,int32_t size,struct thread *t){
  if(fd == 1){
    return write_stdout(buffer,size);
  }
  struct file_struct *file = file_found(fd, t);
  if (file != NULL && !file->isdir) {
    return file_write((struct file *)file->file_dir, buffer,size);
  }
  return -1;
}

/* Reads from stdin into buffer. */
int read_stdin(char *buffer, int32_t size) {
  int num_bytes_to_read = (int) size;
  int i;
  for (i = 0; i < size; i++) {
    *(uint8_t *) buffer[i] = input_getc();

    /* Attempting to halt read when EOF is reached. */
    if (buffer[i] == '\0')
      break;
  }
  return (int) i+1;
}

/*Helps read a file*/
uint32_t sys_read_helper(int fd, char *buffer, int32_t size) {
  if (size <= 0) {
    return 0;
  }
  if (fd == 0) {
    return read_stdin(buffer, size);
  } else if (fd > 2 && fd < MAX_FD) {
    return read_helper(fd, buffer, size, thread_current());
  }
  return 0;
}

/* Given file descriptor, reads from file into buffer. */
int read_helper(int fd, char *buffer, unsigned size, struct thread *t) {
  if ((t->openfds[fd])) {
    sys_helper_exit(-1);
  }
  struct file_struct *file = file_found(fd, t);
  if(file == NULL || file->isdir) {
    sys_helper_exit(-1);
  }
  return file_read((struct file *)file->file_dir, buffer, (int32_t) size);
}



void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}



/* Helper function for exit call. */
void sys_helper_exit(int status) {
  struct thread *cur = thread_current ();
  cur->wait->exit_code = status;
  thread_exit ();
}

/* Validating ADDR. Exit if ADDR is not a valid user vaddr. */
static int
validate_address (void *addr)
{
  if (!addr || !is_user_vaddr (addr)
      || !pagedir_get_page (thread_current ()->pagedir, addr))
    {
      sys_helper_exit(-1);
    }
  return 1;
}

/* Validating addresses of the arguments of the syscall. */
static void
validate_args (void *esp, int argc)
{
  validate_address (esp + (argc + 1) * sizeof (void *) - 1);
}


static void
syscall_handler (struct intr_frame *f UNUSED)
{

  uint32_t* args = ((uint32_t*) f->esp);

  /* Valid the args[0] before going to the switch statement. */
  validate_address((void *)args);
  /* Validate the end of args to make sure that all of args is in user space. */
  validate_address((void *)args + sizeof(void *) - 1);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

    switch(args[0]){
    case SYS_HALT:
      /* Halt the operating system. */
    {
      shutdown_power_off();
      break;
    }
    case SYS_EXIT:
        /* Terminate this process. */
    {
      /* Sets the parent's wait struct's exit code to dying child's status. */
      validate_args(f->esp,1);
      sys_helper_exit((int) args[1]);
      break;
    }
    case SYS_EXEC:
      /* Start another process. */
    {
      validate_args(f->esp, 1);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = process_execute((char *) args[1]);
      break;
    }
    case SYS_WAIT:
      /* Wait for a child process to die. */
    {
      validate_args(f->esp,1);
      f->eax = process_wait(args[1]);
      break;
    }
    case SYS_PRACTICE:
      /* Returns arg incremented by 1 */
    {
      validate_args(f->esp,1);
      f->eax = args[1] + 1;
      break;
    }
    default:
    syscall_helper(f);
  }

}

/* File-related syscalls. */
void syscall_helper(struct intr_frame *f) {
  uint32_t* args = ((uint32_t*) f->esp);
  switch(args[0]) {
    case SYS_CREATE:
        /* Create a file. */
    {
      validate_args(f->esp,2);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = filesys_create((char *)args[1],(unsigned) args[2]);
      break;
    }
    case SYS_REMOVE:
      /* Delete a file. */
    {
      validate_args(f->esp,1);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = sys_remove_helper((char *)args[1]);
      break;
    }
    case SYS_OPEN:
      /* Open a file. */
      /* Also adds the file descriptor to the thread's FD list. */
    {
      validate_args(f->esp,1);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = sys_open_helper((char *)args[1], thread_current());
      break;
    }
    case SYS_FILESIZE:
      /* Obtain a file's size. One argument: FD */
    {
      validate_args(f->esp,1);
      struct thread *current = thread_current();
      int fd = (int) args[1];
      f->eax = 0;
      if (fd > 2 && fd < MAX_FD) {
        bool file_notopen = current->openfds[fd];
        if (!file_notopen) {
          struct file_struct * file = file_found(fd,current);
          if(file == NULL){
            f->eax = -1;
          } else{
            f->eax = file_length((struct file *)file->file_dir);
          }
        }
      }

      break;
    }
    case SYS_READ:
      /* Read from a file. */
    {
      validate_args(f->esp,3);
      int fd = (int)args[1];
      char *buffer = (char *) args[2];
      int32_t size = (int32_t)args[3];
      for(int i = 0; validate_address((void *) args[2] + i) && i < size; i++);
      f->eax = sys_read_helper(fd, buffer, size);
      break;
    }
    case SYS_WRITE:
      /* Write to a file. */
    {
      validate_args(f->esp,3);
      int fd = (int)args[1];
      char *buffer = (char *) args[2];
      int32_t size = (int32_t)args[3];
      validate_address((void *) args[2]);
      for(int i = 0; validate_address((void *) args[2] + i) && i < size; i++);
      struct thread * current = thread_current();
      f->eax = sys_write_helper(fd,buffer,size,current);
      break;
    }
    case SYS_SEEK:
      /* Change position in a file. */
    {
      validate_args(f->esp,2);
      struct thread *current = thread_current();
      int fd = (int) args[1];
      off_t position = (off_t) args[2];

      if (fd > 2 && fd < MAX_FD) {
        struct file_struct *file = file_found(fd, current);
        if (file != NULL && !file->isdir) {
          if (position >= 0 ) { // removed from if statement && position < file_length(file)
            file_seek((struct file *)file->file_dir, position);
          }
        }
      }

      break;
    }
    case SYS_TELL:
      /* Report current position in a file. */
    {
      validate_args(f->esp,3);
      struct thread *t = thread_current();
      int fd = (int)args[1];
      f->eax = 0;
      struct file_struct *cur_file = file_found(fd,t);
      if(cur_file != NULL && !cur_file->isdir) {
        f->eax = (uint32_t) file_tell((struct file *) cur_file->file_dir);
      }
      break;
    }
    case SYS_CLOSE:
      /* Close a file. */
    {
      validate_args(f->esp,1);
      struct thread *t = thread_current();
      f->eax = close_helper((int)args[1], t);
      break;
    }
    case SYS_INUMBER:
    {
      validate_args(f->esp,1);
      struct thread *t = thread_current();
      f->eax = sys_inumber_helper((int)args[1], t);
      break;
    }
    case SYS_ISDIR:
    {
      validate_args(f->esp,1);
      struct thread *t = thread_current();
      f->eax = sys_idir_helper((int)args[1], t);
      break;

    }
    case SYS_READDIR:
    {
      validate_args(f->esp,2);
      for(char *curr= (char *)args[2]; validate_address(curr) && *curr != '\0'; curr++);
      struct thread *current = thread_current();
      f->eax = sys_readdir_helper((int)args[1], (char *) args[2], current);
      break;
    }
    case SYS_MKDIR:
    {
      validate_args(f->esp,1);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = sys_mkdir_helper((char *)args[1]);
      break;

    }
    case SYS_CHDIR:
    {
      validate_args(f->esp,1);
      for(char *curr= (char *)args[1]; validate_address(curr) && *curr != '\0'; curr++);
      f->eax = sys_chdir_helper((char *)args[1]);
      break;
    }
    case SYS_HIT_RATE:
    {
      validate_args(f->esp,1);
      f->eax =  get_hit_rate();
      reset_cache();
      break;
    }
    case SYS_DEVICE_WRITES:
    {
      validate_args(f->esp,1);
      f->eax = get_device_writes();
      break;
    }
    default:
    {
      sys_helper_exit(-1);
      break;
    }

  }

}
