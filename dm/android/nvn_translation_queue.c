#include "nvn_translation_queue.h"
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static void checked(int result) { if (result) abort(); }
static _Thread_local uint64_t current_ticket;
uint64_t dm_nvn_translation_current_ticket(void) { return current_ticket; }
static void invoke(dm_nvn_translation_queue *q,void *payload,uint64_t ticket) {
    current_ticket=ticket;
    q->run(payload);
    current_ticket=0;
}
/* 把翻译线程顶到高优先级大核上。对应 iOS 的 QOS_CLASS_USER_INTERACTIVE（hle_nvn.c:1310）——
 * 那边一条 set_qos_class 就够，Android 要手动做两件事：
 *   1) 绑到「性能核」：按 cpuinfo 的 max freq 排序，取前半段里最快的那几个。
 *      硬编码 cpu 4-7 在骁龙 8 Gen 3 上对，在别的 SoC 上不一定；这里按实际频率分组。
 *   2) 提 nice 值：setpriority(-10) 普通应用可用（不需要 root），
 *      SCHED_FIFO 需要 CAP_SYS_NICE，普通应用拿不到，所以不去碰它。
 * 全部失败也照常跑（只是没有加速），不改变功能语义。
 * Lift the translation thread onto high-priority big cores, the counterpart of iOS's
 * QOS_CLASS_USER_INTERACTIVE (hle_nvn.c:1310). There one set_qos_class call suffices; on Android
 * two steps are needed: (1) pin to the performance cores, chosen by actual max frequency from
 * cpuinfo rather than a hard-coded cpu 4-7 (right on Snapdragon 8 Gen 3, not everywhere);
 * (2) raise the nice value — setpriority(-10) works for an ordinary app, while SCHED_FIFO needs
 * CAP_SYS_NICE and is left alone. Any failure just means no speed-up; behaviour is unchanged. */
static void lift_translation_thread(void) {
    /* 读每个 cpu 的 max freq，取中位数以上的一组当性能核。 */
    unsigned max_khz[64], ncpu = 0;
    for (unsigned cpu = 0; cpu < 64; cpu++) {
        char path[96];
        snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%u/cpufreq/cpuinfo_max_freq", cpu);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        unsigned khz = 0;
        if (fscanf(f, "%u", &khz) == 1 && khz) max_khz[ncpu++] = khz;
        fclose(f);
    }
    if (!ncpu) return;
    /* 以最高频的 80% 为门槛挑性能核（同簇的频率一致，跨簇才会差很多）。 */
    unsigned top = 0;
    for (unsigned i = 0; i < ncpu; i++) if (max_khz[i] > top) top = max_khz[i];
    cpu_set_t set; CPU_ZERO(&set);
    unsigned chosen = 0;
    for (unsigned cpu = 0; cpu < ncpu; cpu++)
        if (max_khz[cpu] * 10 >= top * 8) { CPU_SET(cpu, &set); chosen++; }
    int affinity = chosen ? sched_setaffinity(0, sizeof set, &set) : -1;
    int nice_rc = setpriority(PRIO_PROCESS, 0, -10);
    errno = 0;
    fprintf(stderr, "[nvn-android] TRANSLATE_AFFINITY cpus=%u of %u set=%d nice=-10 rc=%d\n",
            chosen, ncpu, affinity, nice_rc);
}
static void *translate(void *arg) {
    dm_nvn_translation_queue *q = arg;
    pthread_setname_np(pthread_self(), "nvn-translate");
    lift_translation_thread();
    checked(pthread_mutex_lock(&q->mutex));
    for (;;) {
        while (q->completed == q->submitted && !q->stopping) {
            if (q->idle) q->idle(1);
            checked(pthread_cond_wait(&q->changed, &q->mutex));
            if (q->idle) q->idle(0);
        }
        if (q->completed == q->submitted && q->stopping) break;
        uint64_t ticket = q->completed + 1;
        void *payload = q->jobs[ticket % DM_NVN_TRANSLATION_CAPACITY];
        checked(pthread_mutex_unlock(&q->mutex));
        invoke(q,payload,ticket);
        checked(pthread_mutex_lock(&q->mutex));
        q->completed = ticket;
        checked(pthread_cond_broadcast(&q->changed));
    }
    checked(pthread_mutex_unlock(&q->mutex));
    return NULL;
}

int dm_nvn_translation_init(dm_nvn_translation_queue *q, void (*run)(void *), int synchronous,
                            void (*idle)(int), void (*prepare)(void *,uint64_t)) {
    memset(q, 0, sizeof(*q));
    q->run = run;
    q->idle = idle;
    q->prepare = prepare;
    q->synchronous = synchronous != 0;
    checked(pthread_mutex_init(&q->mutex, NULL));
    pthread_condattr_t condition;
    checked(pthread_condattr_init(&condition));
    checked(pthread_condattr_setclock(&condition, CLOCK_MONOTONIC));
    checked(pthread_cond_init(&q->changed, &condition));
    checked(pthread_condattr_destroy(&condition));
    if (q->synchronous) return 0;
    pthread_attr_t attr;
    checked(pthread_attr_init(&attr));
    checked(pthread_attr_setstacksize(&attr, 8u << 20));
    int error = pthread_create(&q->thread, &attr, translate, q);
    checked(pthread_attr_destroy(&attr));
    if (error) q->synchronous = 1;
    return error;
}

uint64_t dm_nvn_translation_push(dm_nvn_translation_queue *q, void *payload) {
    checked(pthread_mutex_lock(&q->mutex));
    while (q->submitted - q->completed >= DM_NVN_TRANSLATION_CAPACITY)
        checked(pthread_cond_wait(&q->changed, &q->mutex));
    if (q->stopping || q->submitted == UINT64_MAX) abort();
    uint64_t ticket = ++q->submitted;
    if (q->prepare) q->prepare(payload,ticket);
    if (q->synchronous) {
        /* Multiple fallback producers still execute in ticket order. Never
         * hold the queue mutex during translation: timed waits must work. */
        while (q->completed != ticket - 1)
            checked(pthread_cond_wait(&q->changed, &q->mutex));
        checked(pthread_mutex_unlock(&q->mutex));
        invoke(q,payload,ticket);
        checked(pthread_mutex_lock(&q->mutex));
        q->completed = ticket;
    } else {
        q->jobs[ticket % DM_NVN_TRANSLATION_CAPACITY] = payload;
    }
    checked(pthread_cond_broadcast(&q->changed));
    checked(pthread_mutex_unlock(&q->mutex));
    return ticket;
}

uint64_t dm_nvn_translation_tail(dm_nvn_translation_queue *q) {
    checked(pthread_mutex_lock(&q->mutex));
    uint64_t ticket = q->submitted;
    checked(pthread_mutex_unlock(&q->mutex));
    return ticket;
}

int dm_nvn_translation_wait(dm_nvn_translation_queue *q, uint64_t ticket, uint64_t timeout_ns) {
    struct timespec deadline = {0};
    if (timeout_ns && timeout_ns != UINT64_MAX) {
        checked(clock_gettime(CLOCK_MONOTONIC, &deadline));
        uint64_t ns = (uint64_t)deadline.tv_nsec + timeout_ns % 1000000000;
        deadline.tv_sec += (time_t)(timeout_ns / 1000000000 + ns / 1000000000);
        deadline.tv_nsec = (long)(ns % 1000000000);
    }
    checked(pthread_mutex_lock(&q->mutex));
    while (q->completed < ticket) {
        if (!timeout_ns) break;
        int error = timeout_ns == UINT64_MAX ? pthread_cond_wait(&q->changed, &q->mutex) :
            pthread_cond_timedwait(&q->changed, &q->mutex, &deadline);
        if (error == ETIMEDOUT) break;
        checked(error);
    }
    int complete = q->completed >= ticket;
    checked(pthread_mutex_unlock(&q->mutex));
    return complete;
}

void dm_nvn_translation_destroy(dm_nvn_translation_queue *q) {
    checked(pthread_mutex_lock(&q->mutex));
    q->stopping = 1;
    checked(pthread_cond_broadcast(&q->changed));
    checked(pthread_mutex_unlock(&q->mutex));
    if (!q->synchronous) checked(pthread_join(q->thread, NULL));
    checked(pthread_cond_destroy(&q->changed));
    checked(pthread_mutex_destroy(&q->mutex));
}
