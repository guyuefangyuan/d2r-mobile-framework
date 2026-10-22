#ifndef NVN_VK_MEMORY_H
#define NVN_VK_MEMORY_H
#include <stdint.h>

/* Backend-owned VkDeviceMemory allocations, not physical residency or driver
 * mtrack. Sizes are the actual VkMemoryRequirements allocation sizes. */
enum {
    DM_VK_MEMORY_TEXTURE, DM_VK_MEMORY_SAMPLE_COPY, DM_VK_MEMORY_SHADOW,
    DM_VK_MEMORY_POOL, DM_VK_MEMORY_SUPPORT, DM_VK_MEMORY_TEMP,
    DM_VK_MEMORY_WRITEBACK, DM_VK_MEMORY_READBACK, DM_VK_MEMORY_PRESENT,
    DM_VK_MEMORY_CLASS_COUNT
};
typedef struct {
    uint32_t abi_version,class_count;
    uint64_t bytes[DM_VK_MEMORY_CLASS_COUNT],peak_bytes[DM_VK_MEMORY_CLASS_COUNT];
    uint64_t allocations[DM_VK_MEMORY_CLASS_COUNT];
    uint64_t total_bytes,peak_total_bytes;
    uint64_t cpu_baseline_bytes,cpu_baseline_peak_bytes;
    uint64_t temp_recording_bytes,temp_inflight_bytes,temp_retired_bytes;
    uint64_t trim_count,last_trim_freed_bytes,last_trim_ns;
} DmVkMemoryStats;
enum {
    DM_VK_TRIM_RETIRED=1u, DM_VK_TRIM_DERIVED=2u, DM_VK_TRIM_WRITEBACK=4u,
    /* 池镜像（guest 内存的 Vulkan 副本，进局后实测常驻 1.1 GB 以上）。
     * 它是 guest 内存的副本、随时可用 pool_materialize 重建，所以切后台时可以整块丢掉：
     * 进程 PSS 从 1.9 GB 降下来，系统就不会因内存压力把进程杀掉，回前台不用重新进局。
     * 代价是回来的第一帧要把用到的部分重新上传（比重新加载整个游戏快得多）。
     * 只在「已确认没有在飞/录制中的提交」时允许（trim 本身已做这项检查）。
     * The pool mirror (Vulkan copies of guest memory; 1.1 GB+ resident once in a run). It is a copy
     * of guest memory and can always be rebuilt by pool_materialize, so it can be dropped whole
     * while in the background: process PSS falls from 1.9 GB, the system no longer kills the
     * process under memory pressure, and returning to the foreground does not restart the run. The
     * cost is re-uploading the parts actually used on the first frame back, which is far cheaper
     * than reloading the whole game. Only allowed when no submission is in flight or recording
     * (trim already checks that). */
    DM_VK_TRIM_POOLS=8u,
    DM_VK_TRIM_ALL=7u
};

/* Both functions use trylock and return EBUSY without waiting on a recorder or
 * GPU. They leave *out unchanged on error and never initialize Vulkan. */
int dm_vk_memory_get_stats(DmVkMemoryStats *out);
/* Caller must serialize lifecycle transitions and suspend/drain guest graphics
 * first. Only retired buffers and reconstructable derived images are released;
 * primary textures, guest pools and their CPU baselines remain intact. */
int dm_vk_memory_trim_caches(unsigned flags,DmVkMemoryStats *out);
#endif
