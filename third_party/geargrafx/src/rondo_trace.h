// rondo-nx coverage tracer: records every 8 KB bank image the CPU executes from,
// plus a bitmap of executed instruction starts, plus System Card cd_read calls.
#ifndef RONDO_TRACE_H
#define RONDO_TRACE_H

#include "types.h"

class Memory;

void rondo_trace_init(const char* out_dir);
void rondo_trace_instruction(Memory* memory, u16 pc);
void rondo_trace_save(void);
void rondo_trace_status(char* buf, int len);

#endif
