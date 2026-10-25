// rondo-nx translated-code runtime interface.
#ifndef RONDO_XLAT_RT_H
#define RONDO_XLAT_RT_H

#include "geargrafx_core.h"
#include "geargrafx_core_inline.h"
#include "huc6280.h"
#include "huc6280_inline.h"
#include "memory.h"
#include "memory_inline.h"

typedef int (*XlatBlockFn)(GeargrafxCore* g, HuC6280* c, u16 base);

struct XlatEntry
{
    u8 bank;
    u16 off;
    u16 len;
    const u8* sig;
    XlatBlockFn fn;
};

class RondoXlat
{
public:
    // 0 = continue, 1 = frame finished, 2 = left the expected path
    static inline int Step(GeargrafxCore* g, HuC6280* c, u16 pc, u8 op, const u8* ops, u16 next, int last)
    {
        s_instructions++;
        g->XlatPre();
        u32 cycles = c->XlatInstruction(pc, op, ops);
        if (g->XlatPost(cycles))
            return 1;
        if (!last && c->XlatPC() != next)
            return 2;
        return 0;
    }
    static void Init();
    static int Dispatch(GeargrafxCore* g);
    static void SetEnabled(bool e);
    static unsigned long long s_blocks_run;
    static unsigned long long s_misses;
    static unsigned long long s_instructions;
};

#define XLAT_NAME(n) xlat_b##n
#define XLAT_DECL(n) int xlat_b##n(GeargrafxCore* g, HuC6280* c, u16 base)
#define XLAT_BLOCK(n) XLAT_DECL(n)
#define XLAT_STEP(off, op, ops, nxt, last) \
    { int r_ = RondoXlat::Step(g, c, (u16)(base | (off)), (op), (ops), (u16)(base | (nxt)), (last)); \
      if (r_ == 1) return 1; if (r_ == 2) return 0; }

#endif
