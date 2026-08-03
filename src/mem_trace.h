#ifndef MEM_TRACE_H
#define MEM_TRACE_H

#include <stdint.h>

// Instrumentation hooks for memory_trace.c (used only by the standalone
// trace harness, not the real SDL build). trace_main.c sets mem_trace_pc
// before every cpu_step() and flips mem_trace_enabled on/off to bound how
// much VIA/AY access logging gets printed.
extern int mem_trace_enabled;
extern uint16_t mem_trace_pc;
extern long mem_trace_step;

#endif // MEM_TRACE_H
