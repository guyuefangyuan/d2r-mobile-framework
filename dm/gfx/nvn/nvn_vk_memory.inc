/* Included after the backend's allocation and submission helpers. */
static void memory_snapshot_ownership(DmVkMemoryStats *out) {
    out->temp_retired_bytes=G.retired_bytes;out->temp_recording_bytes=out->temp_inflight_bytes=0;
    VkSubmission *contexts[2]={&G.submission_slot,&G.submission_slot2};
    for(unsigned k=0;k<2;k++){
        VkSubmission *sub=contexts[k];uint64_t bytes=0;
        for(int i=0;i<sub->ntemporary;i++)bytes+=sub->temporaries[i]->allocation_size;
        if(sub->phase==SUBMISSION_RECORDING)out->temp_recording_bytes+=bytes;
        else out->temp_inflight_bytes+=bytes;
    }
}
int dm_vk_memory_get_stats(DmVkMemoryStats *out) {
    if(!out)return EINVAL;
    if(pthread_mutex_trylock(&G.lock))return EBUSY;
    if(pthread_mutex_trylock(&memory_mu)){pthread_mutex_unlock(&G.lock);return EBUSY;}
    *out=memory_stats;pthread_mutex_unlock(&memory_mu);memory_snapshot_ownership(out);
    pthread_mutex_unlock(&G.lock);return 0;
}
int dm_vk_memory_trim_caches(unsigned flags,DmVkMemoryStats *out) {
    /* 允许的位 = ALL（1|2|4）加上 POOLS（8）。POOLS 不并进 ALL：默认的 ALL 语义要保持不变
     * （只丢可重建的派生资源），池镜像要显式点名才丢。
     * Allowed bits = ALL (1|2|4) plus POOLS (8). POOLS is not folded into ALL so the default ALL
     * semantics stay as they were (only reconstructable derived resources); the pool mirror is
     * dropped only when explicitly requested. */
    if(flags&~(unsigned)(DM_VK_TRIM_ALL|DM_VK_TRIM_POOLS))return EINVAL;    if(pthread_mutex_trylock(&G.lock))return EBUSY;
    VkSubmission *contexts[2]={&G.submission_slot,&G.submission_slot2};
    if(G.active||g_jcount||ghost.n||ghost.busy){pthread_mutex_unlock(&G.lock);return EBUSY;}
    for(unsigned k=0;k<2;k++)if(contexts[k]->phase!=SUBMISSION_UNUSED&&contexts[k]->phase!=SUBMISSION_RETIRED){
        pthread_mutex_unlock(&G.lock);return EBUSY;
    }
    uint64_t started=dm_android_nvn_clock_ns(),freed=0;
    /* The caller blocks new guest graphics while this lock is released again.
     * No submission is recorded, in flight or awaiting CPU publication. */
    if(flags&DM_VK_TRIM_RETIRED)while(G.nretired){VBuffer *b=retired_take(G.nretired-1);freed+=b->allocation_size;buffer_free(b);free(b);}
    if(flags&DM_VK_TRIM_DERIVED){resource_cache_clear(contexts[0]);resource_cache_clear(contexts[1]);}
    if(flags&DM_VK_TRIM_DERIVED)for(uint32_t i=0;i<G.texture_used;i++)if(G.textures[i].serial)freed+=texture_derived_release(&G.textures[i]);
    if(flags&DM_VK_TRIM_WRITEBACK)for(unsigned k=0;k<2;k++){
        if(contexts[k]->wb_stage.m)freed+=contexts[k]->wb_stage.allocation_size;
        buffer_free(&contexts[k]->wb_stage);contexts[k]->wb_staged=0;contexts[k]->wb_stage_total=0;
    }
    if(flags&DM_VK_TRIM_POOLS){
        /* 释放池镜像本身（guest 内存的 Vulkan 副本）。保留 VPool 的注册信息（guest 指针、大小、
         * serial、槽位位图）——只丢 v，下一次 pool_read 由 pool_materialize 按需重建。
         * 必须同时清掉增量上传缓存：那些条目记的是「镜像里已有这段」的戳，镜像没了就全不算数，
         * 否则回来时会把旧戳当真、跳过本该做的上传。
         * Release the pool mirror itself (the Vulkan copy of guest memory). The VPool registration
         * (guest pointer, size, serial, slot bitmaps) is kept — only v is dropped, and the next
         * pool_read rebuilds it through pool_materialize. The incremental upload cache must be
         * cleared at the same time: its entries record "the mirror already holds this range", which
         * is meaningless once the mirror is gone, and keeping them would let stale stamps skip
         * uploads that are actually needed. */
        for(int i=0;i<G.npool;i++){
            if(!G.pools[i].v.b)continue;   /* buffer_free 会把整个 VBuffer 清零，所以 v.b 就是「物化过」的判据 | buffer_free zeroes the whole VBuffer, so v.b is the "was materialised" test */
            freed+=G.pools[i].v.allocation_size;
            buffer_free(&G.pools[i].v);
        }
        memset(G.pool_uploads,0,sizeof G.pool_uploads);
        G.pool_revision++;
    }
    if(flags){
        /* Primary image identities and guest registrations survive. Cached
         * descriptors may still name a derived image; rebuild all such plans
         * on the next recording epoch. Executable command buffers are reset
         * by sub_begin_ctx before reuse, after their fences were retired. */
        G.registry_revision++;memset(G.driver_cache,0,sizeof G.driver_cache);
        for(unsigned i=0;i<6;i++){
            G.binding_revision[i]++;G.ssbo_revision[i]++;G.texbind_revision[i]++;
            G.driver_revision[i][0]++;G.driver_revision[i][1]++;
        }
        for(unsigned k=0;k<2;k++)nvn_vk_command_state_reset(&contexts[k]->command_state);
    }
    pthread_mutex_lock(&memory_mu);
    if(flags){
        memory_stats.trim_count++;memory_stats.last_trim_freed_bytes=freed;
        memory_stats.last_trim_ns=dm_android_nvn_clock_ns()-started;
    }
    if(out)*out=memory_stats;
    pthread_mutex_unlock(&memory_mu);
    if(out)memory_snapshot_ownership(out);
    pthread_mutex_unlock(&G.lock);return 0;
}
