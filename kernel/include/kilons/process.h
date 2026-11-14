#ifndef KILONS_PROCESS_H
#define KILONS_PROCESS_H
#include <stdbool.h>
#include <stdint.h>
/* Task contexts/CR3 belong to ring3.c; this table retains zombie status. */
enum process_state { PROCESS_FREE=0, PROCESS_READY, PROCESS_RUNNING, PROCESS_BLOCKED, PROCESS_ZOMBIE };
bool process_create_child(uint32_t pid,uint32_t parent);
/* Roll back an unpublished READY task when fork fails. */
bool process_abort(uint32_t pid);
bool process_start(uint32_t pid);
bool process_suspend(uint32_t pid);
bool process_block(uint32_t pid);
bool process_wake(uint32_t pid);
bool process_exit(uint32_t pid,int status);
bool process_is_child(uint32_t parent,uint32_t pid);
uint32_t process_parent_of(uint32_t pid);
/* On success collect() returns true; collect_any() returns PID, 0 if
 * children exist but none are zombies, -10 if there are no children. */
bool process_collect(uint32_t parent,uint32_t pid,int *status);
int process_wait_event(uint32_t,int,uint32_t,int *,bool,uint32_t *);
void process_note_uid(uint32_t,uint32_t,uint32_t);
int process_wait_events(uint32_t,int,uint32_t,int *,bool,uint32_t *,unsigned);
bool process_is_stopped(uint32_t);
void process_stop(uint32_t,int);
int process_send_signal(uint32_t,int,int,uint32_t,uint32_t,void (*)(uint32_t,int));
int process_job_signal(uint32_t,int,int,uint32_t,uint32_t);
int process_collect_selected(uint32_t parent,int selector,int *status);
int process_collect_group(uint32_t parent,uint32_t group,int *status);
void process_mark_exec(uint32_t pid);
int process_group_id(uint32_t caller,int pid,bool session);
int process_set_group(uint32_t caller,int pid,int pgid);
int process_new_session(uint32_t caller);
int process_collect_any(uint32_t parent,int *status);
enum process_state process_state_of(uint32_t pid);
unsigned process_live_count(void);
unsigned process_zombie_count(void);
unsigned process_ready_count(void);
unsigned process_blocked_count(void);
void process_set_reaper(uint32_t pid);
/* Check PID uniqueness, reparenting, zombies and record integrity. */
bool process_validate(void);
#endif
