#ifdef CONFIG_GDB_STUB
#ifndef __GDB_STUB_H__
#define __GDB_STUB_H__

#define GDB_STUB_INIT_SUCCESS  0
#define GDB_STUB_INIT_FAILURE -1

int gdb_stub_init(void);
void gdb_stub_event_handler(trapframe_t *trap_frame);

#endif /* __GDB_STUB_H__ */
#endif /* CONFIG_GDB_STUB */
