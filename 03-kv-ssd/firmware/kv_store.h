// [prj3] KV-SSD index definition (new file)
#ifndef KV_STORE_H_
#define KV_STORE_H_

#include <stdint.h>
#include "memory_map.h"

// [prj3] Direct-mapped index: one array entry for every key (4,194,304 keys).
//        Keys are 4-byte integers from 0 to 4M-1, so the key itself is the array index -> O(1) lookup.
#define MAX_KV_ENTRIES 4194304

// [prj3] The index lives in reserved DRAM after the FTL area (RESERVED1 to the end of DRAM).
//        12 B x 4M entries = about 48 MB, which fits easily.
#define KV_INDEX_ADDR  RESERVED1_START_ADDR

// [prj3] Where one key's value is. The value itself is on NAND; the index keeps only its LSA.
typedef struct {
	uint32_t lsa;          // LSA where the value is written
	uint32_t value_len;    // value length in bytes
	uint8_t valid;         // set once the key has been PUT
} KV_INDEX_ENTRY;

extern KV_INDEX_ENTRY* kv_index;
extern uint32_t kv_current_lsa;

void InitKvStore();

#endif
