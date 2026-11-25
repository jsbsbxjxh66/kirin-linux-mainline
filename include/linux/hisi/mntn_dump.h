#ifndef __MNTN_DUMP_H
#define __MNTN_DUMP_H
#include <linux/hisi/mntn_public_interface.h>

#ifdef CONFIG_HISI_MNTNDUMP
int mntn_dump_init(void);
extern int register_mntn_dump(int mod_id, unsigned int size, void **vaddr);
#else
static inline int mntn_dump_init(void){return -1;}
static inline int register_mntn_dump(int mod_id, unsigned int size, void **vaddr){return -1;}
#endif

#endif
