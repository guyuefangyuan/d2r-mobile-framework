#pragma once
#include <stdint.h>

/* The explicit 60 FPS experiment ignores the guest's present interval, but
 * remains capped. Otherwise preserve the original Android cadence exactly,
 * including its integer rounding and the headless/test hz=0 escape hatch. */
static inline uint64_t dm_android_present_period_ns(int base_hz,
        int apply_guest_interval, int guest_interval, int fixed_60) {
    if(fixed_60)return 1000000000ULL/60;
    if(!base_hz)return 0;
    uint64_t interval=guest_interval>0?(uint64_t)guest_interval:1;
    return (1000000000ULL/(uint64_t)base_hz)*(apply_guest_interval?interval:1);
}

/* Power saving is a cap, so a slower guest/GPU never runs faster when it is
 * enabled. An explicit uncapped diagnostic stays uncapped.
 *
 * 这里原来只有「省电 30 帧」两档（eco_fps 是布尔值），所以写死了 1e9/30。
 * 现在帧率档位有三档（60 / 40 / 30），参数改成目标帧率本身：0 表示不限（保持原样），
 * 否则取「guest 周期」与「目标周期」里更长的那一个——即只封顶、不加速。
 * This used to have only two tiers (the boolean eco_fps for 30 fps), so 1e9/30 was hard-coded.
 * The frame-rate tier now has three values (60 / 40 / 30), so the parameter is the target rate
 * itself: 0 means uncapped (leave the period alone); otherwise take the longer of the guest period
 * and the target period — a cap only, never a speed-up. */
static inline uint64_t dm_android_present_power_period_ns(uint64_t guest_period,
        int target_hz) {
    if(!target_hz)return guest_period;
    const uint64_t target_period=1000000000ULL/(uint64_t)target_hz;
    return guest_period && guest_period<target_period ? target_period : guest_period;
}
