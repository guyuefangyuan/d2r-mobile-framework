/* Included by hle_nvn.c only on Android: virtual NVN objects/pacing are retained;
 * no version-94012 game-address hooks or Darwin instrumentation is linked.
 */
#include "nvn_platform_clock.h"
#include "nvn_platform_backend.h"
#include "nvn_present_policy.h"
#include "nvn_translation_queue.h"
#include "nvn_translation_sync.h"
#include "scale3d_android.h"
#include "../gfx/nvn/nvn_vk_memory.h"
static pthread_mutex_t android_suspend_mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t android_suspend_cv=PTHREAD_COND_INITIALIZER;
static pthread_mutex_t android_lifecycle_mu=PTHREAD_MUTEX_INITIALIZER;
static int android_suspended,android_inflight;
static _Atomic uint64_t android_nvn_frame;
enum { ANDROID_X_SUBMIT, ANDROID_X_PRESENT, ANDROID_X_BEGIN, ANDROID_X_FENCE };
typedef struct {
    int kind, n;
    const tex_t *texture;
    sync_t *sync;
    double period;
    int crop[4];
    NVNcommandHandle handles[];
} android_xjob;
static dm_nvn_translation_queue android_xqueue;
static pthread_once_t android_xonce=PTHREAD_ONCE_INIT;
static void android_xrun(void *payload) {
    android_xjob *j=payload;
    dm_android_nvn_schedule_thread("translate");
    switch(j->kind) {
    case ANDROID_X_SUBMIT:
        dm_android_nvn_work_check_thread();
        mtl_sub_begin();
        mtl_sub_index(j->handles,j->n);
        for(int i=0;i<j->n;i++)mtl_exec((const uint8_t *)(uintptr_t)j->handles[i]);
        mtl_sub_commit_async();
        /* Vulkan retains bound program/target snapshots across submissions.
         * Do not reclaim them using Apple's submission-count snap_tick. */
        break;
    case ANDROID_X_PRESENT: {
        dm_android_nvn_work_check_thread();
        mtl_present(j->texture,j->period,j->crop);
        dm_android_scale3d_present(now_ns(),(uint64_t)(j->period*1e9));
        uint64_t frame=atomic_fetch_add(&android_nvn_frame,1)+1;
        mtl_set_frame(frame);dm_android_nvn_presented(frame);
        dm_android_nvn_work_end(frame);break;
    }
    case ANDROID_X_BEGIN: dm_android_nvn_work_begin();break;
    case ANDROID_X_FENCE:
        /* Events, readbacks and presents also allocate Vulkan fence values.
         * Capture the real backend fence here, in translation queue order. */
        dm_nvn_sync_publish(j->sync,mtl_last_fence());break;
    default: die("Android translation job kind %d",j->kind);
    }
    free(j);
}
static void android_xidle(int waiting) {
    static _Thread_local uint64_t token;
    if(waiting)token=dm_android_nvn_work_wait_begin(DM_NVN_WAIT_TRANSLATION_QUEUE);
    else dm_android_nvn_work_wait_end(token);
}
static void android_xprepare(void *payload,uint64_t ticket) {
    (void)ticket;
    android_xjob *j=payload;
    if(j->kind==ANDROID_X_FENCE)atomic_fetch_add(&j->sync->pending,1);
    if(j->kind==ANDROID_X_SUBMIT)for(int i=0;i<j->n;i++) {
        if(!dm_nvn_mark_fences((const uint8_t *)(uintptr_t)j->handles[i],0))
            die("Android invalid command stream while indexing syncs handle=%d",i);
    }
}
static void android_xstart(void) {
    const char *e=getenv("DM_NVN_SYNC");
    int error=dm_nvn_translation_init(&android_xqueue,android_xrun,e&&atoi(e),android_xidle,android_xprepare);
    LOG("[nvn-android] TRANSLATION_QUEUE mode=%s capacity=%d thread_error=%d",
        android_xqueue.synchronous?"synchronous":"worker",DM_NVN_TRANSLATION_CAPACITY,error);
}
static android_xjob *android_xnew(int kind,int n) {
    if(n<0 || (size_t)n>(SIZE_MAX-sizeof(android_xjob))/sizeof(NVNcommandHandle))die("Android translation handle count %d",n);
    android_xjob *j=calloc(1,sizeof(*j)+(size_t)n*sizeof(NVNcommandHandle));
    if(!j)die("Android translation job allocation n=%d",n);
    j->kind=kind;j->n=n;return j;
}
static uint64_t android_xpush(android_xjob *j) {
    pthread_once(&android_xonce,android_xstart);
    return dm_nvn_translation_push(&android_xqueue,j);
}
static void x_drain(void) {
    pthread_once(&android_xonce,android_xstart);
    dm_nvn_translation_wait(&android_xqueue,dm_nvn_translation_tail(&android_xqueue),UINT64_MAX);
}
static void android_queue_fence(sync_t *s) {
    android_xjob *j=android_xnew(ANDROID_X_FENCE,0);j->sync=s;android_xpush(j);
}
static NVNsyncWaitResult android_sync_wait(const sync_t *s,uint64_t timeout) {
    uint64_t start=now_ns();
    uint64_t generation=atomic_load(&s->pending);
    int ready=atomic_load(&s->translated)>=generation;
    /* Wait only for this fence to be reached, not for the queue tail: a later
     * GPU event may itself need the CPU that is waiting here to signal it. */
    while(atomic_load(&s->translated)<generation) {
        if(timeout!=UINT64_MAX && now_ns()-start>=timeout)return NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
        struct timespec pause={0,100000};nanosleep(&pause,NULL);
    }
    if(!s->fenced)return NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
    if(mtl_wait(s->fence,0))return ready?NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED:NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED;
    uint64_t elapsed=now_ns()-start;
    if(timeout!=UINT64_MAX){if(elapsed>=timeout)return NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;timeout-=elapsed;}
    return mtl_wait(s->fence,timeout)?NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED:NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED;
}
static void android_gfx_enter(void) {
    pthread_mutex_lock(&android_suspend_mu);
    while(android_suspended)pthread_cond_wait(&android_suspend_cv,&android_suspend_mu);
    android_inflight++;pthread_mutex_unlock(&android_suspend_mu);
}
static void android_gfx_leave(void) {
    pthread_mutex_lock(&android_suspend_mu);android_inflight--;
    pthread_cond_broadcast(&android_suspend_cv);pthread_mutex_unlock(&android_suspend_mu);
}
void dm_gfx_suspend(int on) {
    pthread_mutex_lock(&android_lifecycle_mu);
    /* Publish the new workload epoch before waking a resumed acquire caller.
     * Hint service calls never hold android_suspend_mu. */
    if(!on) {void dm_3d_scale_resumed(void);dm_3d_scale_resumed();dm_android_nvn_work_reset(1);}
    pthread_mutex_lock(&android_suspend_mu);android_suspended=on!=0;
    if(on)while(android_inflight)pthread_cond_wait(&android_suspend_cv,&android_suspend_mu);
    else pthread_cond_broadcast(&android_suspend_cv);
    pthread_mutex_unlock(&android_suspend_mu);if(on){
        x_drain();dm_android_nvn_work_reset(0);mtl_drain();dm_vk_pipeline_cache_save();
        DmVkMemoryStats memory;
        /* 切后台时把池镜像也丢掉（DM_VK_TRIM_POOLS）：那 1.1 GB 是 guest 内存的副本，
         * 留着只会让进程 PSS 停在 1.9 GB 左右，而 Android 在内存压力下会直接杀进程
         * ——回前台就变成重新进局。丢掉之后 PSS 大幅下降，进程能活下来；回到前台的
         * 第一帧由 pool_materialize 按需重建并重新上传用到的部分。
         * 实测（骁龙 8G3 平板 8 GB）：丢之前 MemFree 只剩 131 MB，系统 lowmemorykiller
         * 连续触发后进程被杀（PID 变化、status.json 消失）。
         * Drop the pool mirror as well when going to the background (DM_VK_TRIM_POOLS): those
         * 1.1 GB are copies of guest memory, and keeping them holds process PSS near 1.9 GB while
         * Android kills the process under memory pressure — coming back then means restarting the
         * run. Dropping them cuts PSS sharply so the process survives, and the first frame back
         * rebuilds and re-uploads only the parts actually used. Measured on a Snapdragon 8G3
         * tablet with 8 GB: before the drop MemFree was down to 131 MB and lowmemorykiller
         * fired repeatedly until the process died (PID changed, status.json gone). */
        int error=dm_vk_memory_trim_caches(DM_VK_TRIM_ALL|DM_VK_TRIM_POOLS,&memory);
        if(!error)LOG("ANDROID_GPU_CACHE_TRIM freed=%llu remaining=%llu count=%llu",
            (unsigned long long)memory.last_trim_freed_bytes,
            (unsigned long long)memory.total_bytes,(unsigned long long)memory.trim_count);
        else LOG("ANDROID_GPU_CACHE_TRIM skipped errno=%d",error);
    }
    pthread_mutex_unlock(&android_lifecycle_mu);
}
uint64_t dm_nvn_frame_count(void) {return atomic_load(&android_nvn_frame);}
NVN(nvnQueueSubmitCommands,void,NVNqueue *q,int n,const NVNcommandHandle *handles) {
    if(!q || n<0 || (n&&!handles))die("Android NVN submit parameters n=%d",n);
    android_gfx_enter();
    android_xjob *j=android_xnew(ANDROID_X_SUBMIT,n);
    if(n)memcpy(j->handles,handles,(size_t)n*sizeof(*handles));
    ((queue_t *)q)->submits++;android_xpush(j);android_gfx_leave();
}
__attribute__((constructor)) static void android_nvn_register(void) {
    nvn_init_slots();
    unsigned profiled=0;
    if(getenv("DM_NVN_PROF"))for(unsigned i=0;i<NVN_NFUNC;i++) {
        if(nvn_slot[i].fn!=(void *)nvn_unimpl_entry && strcmp(nvn_names[i],"nvnQueuePresentTexture")) {
            nvn_real[i]=nvn_slot[i].fn;nvn_slot[i].fn=(void *)nvn_prof_entry;profiled++;
        }
    }
    LOG("[nvn-android] ELF interfaces=%u implemented=%u named-stop=%u profiled=%u; zero unknown-success stubs",
        NVN_NFUNC,g_nsem,NVN_NFUNC-g_nsem,profiled);
}
#define WB(b) ((win_builder_t *)(b))
NVN(nvnWindowBuilderSetDefaults,void,NVNwindowBuilder *b) {NVNdevice *d=WB(b)->dev;memset(b,0,sizeof(win_builder_t));WB(b)->dev=d;WB(b)->interval=1;}
NVN(nvnWindowBuilderSetDevice,void,NVNwindowBuilder *b,NVNdevice *d) {WB(b)->dev=d;}
NVN(nvnWindowBuilderSetNativeWindow,void,NVNwindowBuilder *b,NVNnativeWindow native) {WB(b)->native=native;}
NVN(nvnWindowBuilderSetTextures,void,NVNwindowBuilder *b,int n,NVNtexture *const *textures) {
    if(n<1 || n>4 || !textures)die("Android NVN window texture count %d",n);
    WB(b)->ntex=n;
    for(int i=0;i<n;i++){if(!textures[i])die("Android NVN null window texture %d",i);WB(b)->tex[i]=textures[i];}
}
NVN(nvnWindowBuilderSetPresentInterval,void,NVNwindowBuilder *b,int interval) {if(interval<0)die("negative present interval %d",interval);WB(b)->interval=interval;}
static void android_present_log(const win_t *w);
NVN(nvnWindowInitialize,NVNboolean,NVNwindow *window,const NVNwindowBuilder *b) {
    const win_builder_t *s=(const void *)b;if(s->ntex<1 || s->ntex>4)return 0;
    void *native=dm_vk_native_window();
    if(!native || (void *)(uintptr_t)s->native!=native)die("Android NVN window requires currently bound real Surface");
    win_t *w=(void *)window;memset(w,0,sizeof(*w));w->magic=MAGIC('W','I','N','D');w->native=s->native;
    w->ntex=s->ntex;w->interval=s->interval;memcpy(w->tex,s->tex,sizeof(w->tex));android_present_log(w);return 1;
}
NVN(nvnWindowFinalize,void,NVNwindow *window) {x_drain();mtl_finish();memset(window,0,sizeof(win_t));}
NVN(nvnWindowSetPresentInterval,void,NVNwindow *window,int interval) {
    if(interval<0)die("negative present interval %d",interval);
    win_t *w=(void *)window;
    if(w->interval!=interval){w->interval=interval;android_present_log(w);}
}
NVN(nvnWindowGetPresentInterval,int,const NVNwindow *window) {return ((const win_t *)window)->interval;}
NVN(nvnWindowSetCrop,void,NVNwindow *window,int x,int y,int width,int height) {
    if(x<0 || y<0 || width<0 || height<0)die("NVN crop bounds %d %d %d %d",x,y,width,height);
    int crop[4]={x,y,width,height};memcpy(((win_t *)window)->crop,crop,sizeof(crop));
}
static int android_present_hz=-1,android_display_intervals,android_present_fixed_60=-1;
/* The graphics OBJECT library is shared by generic titles and D2R. Only the
 * D2R target defines this policy callback; target defines do not propagate
 * into the independently compiled graphics objects. */
extern int dm_android_d2r_fps_setting(void) __attribute__((weak));
void nvn_present_config(int hz,int display_intervals) {if(hz<0 || hz>1000)die("NVN pacing hz=%d",hz);android_present_hz=hz;android_display_intervals=display_intervals;}
static uint64_t android_present_period(const win_t *w) {
    if(android_present_hz<0) {const char *e=getenv("DM_PRESENT_HZ");android_present_hz=e?atoi(e):60;
        android_display_intervals=mtl_has_window();}
    /* 游戏把 nvnWindowBuilderSetPresentInterval 设成 2，即「每 2 个显示节拍出一帧」，
     * 在 60 Hz 节拍下就是 30 帧。iOS 那边早就把这条当成「剪口」绕过了（DM_PRESENT_INTERVAL=display
     * 时 itv 固定为 1，见 hle_nvn.c 的 present_period），Android 却一直照游戏给的间隔算，
     * 这是两端帧率差异的直接原因之一。这里默认忽略游戏设的间隔（每个节拍出一帧 = 60 帧）；
     * 想看原版语义可以设 DM_ANDROID_GUEST_INTERVAL=1。
     * The game calls nvnWindowBuilderSetPresentInterval(2), i.e. one frame every two display ticks,
     * which on a 60 Hz cadence is exactly 30 fps. iOS has long bypassed this as a "cut"
     * (with DM_PRESENT_INTERVAL=display the interval is pinned to 1, see present_period in hle_nvn.c),
     * while Android kept honouring the guest's interval — one direct cause of the frame-rate gap.
     * Ignore the guest interval by default (one frame per tick = 60 fps); set
     * DM_ANDROID_GUEST_INTERVAL=1 to get the original semantics back. */
    { static int guest_interval_policy=-1;
      if(guest_interval_policy<0){const char *e=getenv("DM_ANDROID_GUEST_INTERVAL");guest_interval_policy=e&&atoi(e)?1:0;}
      if(!guest_interval_policy)android_display_intervals=0; }
    if(android_present_fixed_60<0)android_present_fixed_60=getenv("DM_ANDROID_PRESENT_60FPS")!=NULL;
    uint64_t period=dm_android_present_period_ns(android_present_hz,android_display_intervals,w->interval,android_present_fixed_60);
    /* 每帧读一次已生效的设置；「应用 / 放弃」的语义不变，fixed60 诊断开关优先级最高。
     * 传的是目标帧率本身（60 / 40 / 30），不是「是不是 30」的布尔值——三档都靠它封顶。
     * Read the applied settings once per frame; Apply/Discard keep their semantics and the fixed60
     * diagnostic switch keeps precedence. The value passed is the target frame rate itself
     * (60 / 40 / 30), not a boolean "is it 30" — all three tiers are capped through it. */
    if(!android_present_fixed_60 && dm_android_d2r_fps_setting)
        period=dm_android_present_power_period_ns(period,dm_android_d2r_fps_setting());
    return period;
}
static void android_present_log(const win_t *w) {
    uint64_t period=android_present_period(w);
    LOG("[nvn-android] PRESENT_POLICY mode=%s base_hz=%d guest_interval=%d apply_guest_interval=%d effective_period_ns=%llu",
        android_present_fixed_60?"fixed60-experiment":"guest",android_present_hz,w->interval,
        android_display_intervals&&!android_present_fixed_60,(unsigned long long)period);
}
NVN(nvnWindowAcquireTexture,NVNwindowAcquireTextureResult,NVNwindow *window,NVNsync *sync,int *index) {
    win_t *w=(void *)window;if(!index || w->ntex<1 || w->ntex>4)die("NVN acquire window",0);
    android_gfx_enter();dm_android_nvn_schedule_thread("acquire");*index=w->next++%w->ntex;
    uint64_t now=now_ns(),period=android_present_period(w);nvn_pacer p={w->pace};uint64_t wait=nvn_pace(&p,now,period);w->pace=p.target;
    if(wait)dm_android_nvn_sleep_until(now+wait);
    /* These are translation tickets, not Vulkan fence/present numbers. The
     * backend owns image/GPU reuse; its present_wait only checks ticket validity. */
    if(w->ticket[*index])dm_nvn_translation_wait(&android_xqueue,w->ticket[*index],UINT64_MAX);
    if(sync)android_queue_fence((sync_t *)sync);
    android_xpush(android_xnew(ANDROID_X_BEGIN,0));android_gfx_leave();return NVN_WINDOW_ACQUIRE_TEXTURE_RESULT_SUCCESS;
}
int (*g_nvn_loadscreen_up)(void);
NVN(nvnQueuePresentTexture,void,NVNqueue *q,NVNwindow *window,int index) {
    (void)q;win_t *w=(void *)window;if(index<0 || index>=w->ntex || index>=4 || !w->tex[index])die("NVN present index %d",index);
    android_gfx_enter();android_xjob *j=android_xnew(ANDROID_X_PRESENT,0);
    j->texture=snap_tex(w->tex[index]);j->period=android_present_period(w)/1e9;memcpy(j->crop,w->crop,sizeof(j->crop));
    w->ticket[index]=android_xpush(j);w->presents++;android_gfx_leave();
}
