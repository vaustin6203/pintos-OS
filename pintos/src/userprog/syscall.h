#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define MAX_FD 2048

void syscall_init (void);
void sys_helper_exit(int status);


#endif /* userprog/syscall.h */
