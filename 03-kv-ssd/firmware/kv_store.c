// [prj3] KV-SSD index setup (new file)
#include "kv_store.h"
#include <string.h>
#include "xil_printf.h"

KV_INDEX_ENTRY* kv_index = (KV_INDEX_ENTRY*)KV_INDEX_ADDR;

// [prj3] Next write position of the append-only value log.
//        Each PUT writes its value here, then moves it forward by the blocks written.
//        A new PUT of the same key goes to a new place and only updates the index, so the old value is dropped.
uint32_t kv_current_lsa = 0;

void InitKvStore() {
	memset(kv_index, 0, sizeof(KV_INDEX_ENTRY) * MAX_KV_ENTRIES);
	kv_current_lsa = 0;
	xil_printf("[prj3] KV store initialized\r\n");
}
