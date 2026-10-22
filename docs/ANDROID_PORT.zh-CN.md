# Android 分支：本仓库相对上游做了什么

上游 [dodola/d2r-mobile](https://github.com/dodola/d2r-mobile) 的主线是 iOS / macOS，
Android 是实验分支。这份文档记录本仓库（`jhaizhou-ops/d2r-mobile` 的 Android 工作）
在框架层面加了什么、为什么这么加，以及哪些地方仍然是坑。

> 这里只写框架层的行为与取舍，不含游戏文件的任何内容。测试方法见
> [TESTING.md](../TESTING.md)，范围与版权声明见 [NOTICE.md](../NOTICE.md)。

## 1. 触屏 UI 覆盖层必须随 APK 一起走

游戏的界面布局（`layouts/xtouch_lay` 等三个目录）靠一份改写过的数据包切成触屏版。
这份「触屏 UI 覆盖层」由 `dm_android_custom_overlay()` / `overlay_complete()` 检查，
必须恰好四个文件（`ctrl_touch`、`touch_strings.tsv`、`data/.build.config`、
`data/00-00000007.data`），由 `GameActivity.installOverlay()` 从 APK 的
`assets/overlay/` 复制到 `files/d2r-mobile/romfs-overlay` 才能生效。

这里踩过两个坑，都已修掉：

1. **打包漏了覆盖层**。打包器若不带 `assets/overlay-manifest.json`，
   `installOverlay()` 会静默返回，宿主随后打 `ANDROID_CUSTOM_OVERLAY base_unavailable=1`
   并跑**原版 Switch 手柄界面**：角色面板摆到画面另一侧、可点的「创建新角色」变成 ✚ 键提示，
   于是那台设备**新建不了角色**。现在覆盖层直接写进 APK 归档（不经 `aapt2 -A`，
   因为 aapt2 会跳过点文件 `data/.build.config`）。
2. **失败不可诊断**。`overlay_complete()` 现在逐文件报
   `missing=<文件名> reason=<原因> errno=<错误码>`；`installOverlay()` 会打
   `D2ROverlay: files=<n> rewritten=<n>`，缺清单时打 W 级告警。

## 2. 覆盖层控件按「画面」缩放，而不是按绝对 dp

屏幕上的控制按钮（方向键 / 确认 / 返回 / 开始 / 模式 / 系统 / FPS）原来按固定 dp 摆放。
16:9 画面在 10 寸平板上约 1280dp 宽，在 6.3 寸手机上只有 617dp —— 同一份绝对尺寸在手机上
相对画面大一倍，「返回」「确认」正好压在游戏自己的按钮上。

```java
int[] picture = GameDisplayMetrics.fitGame(metrics.widthPixels, metrics.heightPixels);
overlayScale = Math.max(0.62f, Math.min(1f, picture[0] / metrics.density / 1280f));
```

- 平板：`3200 / 2.5 / 1280 = 1.0` → **完全不变**；
- 手机：`2161 / 3.5 / 1280 = 0.48` → 被 0.62 下限抬到 0.62（再小触控面积就低于 8mm）。

配套的 `overlayPx()` = `dp() × overlayScale`，字号同样乘这个系数。
整屏的加载文字仍用原来的 `dp()`。

## 3. Mali 上不要被驱动的错误报告带走

手机（Mali 系统驱动）在「加载全部职业模型」这类一次性大上传里，`logcat` 会出现
`mali: Received a GROUP_ERROR_FATAL error on group(0)`。之后我们紧接着读遮挡查询：

```
[nvn-vulkan] STOP vkGetQueryPoolResults(G.device, sub->occlusion, ...) (1)
#01 die()  #02 commit_after_gpu()
```

原来这条路径走 `CHECK()` → `die()` → `abort()`，整个会话直接没了。现在改成
`query_read_tolerant()`：驱动给不出结果就把这一帧的采样数按 0 处理、记一条
`QUERY_READ_FAILED`，画面和流程继续。

> 这只是**韧性**，不是修复：Mali 那个错误本身还在，重负载下仍可能掉帧或偶发异常画面。

## 4. 帧率档位与节拍

- 系统菜单提供 30 / 40 / 60 三档，存 `files/d2r-mobile/diagnostics/present-hz.txt`
  （`RenderFpsStore`，AtomicFile + 回读校验）。
- 节拍只有整数分频可用（120Hz 屏 → 120/60/40/30），见
  `dm/android/nvn_present_policy.h` 的 `dm_android_present_power_period_ns()`，
  语义是**只封顶、不抬高**。
- 3D 内部分辨率另有 `ANDROID_3D_SCALE` 动态档（缺省 0.75，下限 0.6），
  与帧率档位正交。

## 5. 位图池与上传

- 位图池的「已用」标记改成按字批量置位（`pool_use_mark()`），范围判断同样按字批量
  （`bitmap_any()`）；实测 `usemark_ms` 274 → 33.8（−88%）。
- 池页粒度运行期可调（`DM_SLOT_PAGE_SHIFT` 6..16，标记文件
  `diagnostics/slot-page-shift.txt`）。
- 挂起时释放池镜像（`dm_vk_memory_trim_caches(DM_VK_TRIM_ALL|DM_VK_TRIM_POOLS)`），
  避免后台被 `lowmemorykiller` 杀掉导致回来重载。

## 6. HUD 资源

技能圈 / 血球字形 / 摇杆图来自 `assets/d2r_hud/`（由 `dm/android/tools/stage_hud_assets.py`
从你自己的一份游戏资源里生成，`dm/android/tools/` 下另有说明）。缺了它技能圈全黑。
**仓库不附带这些资源。**

## 7. 诊断标记

`files/d2r-mobile/diagnostics/` 下放空文件即可开对应日志：

| 标记 | 作用 |
|---|---|
| `present-hz.txt` | 帧率档位（内容是 `30` / `40` / `60`） |
| `slot-page-shift.txt` | 位图池页粒度 |
| `render-slots.txt` | 渲染槽位数（1..4） |
| `profile-detail` | 细分计时（关着时子项都是 0.000，容易误判） |
| `pool-range-stats` | 池范围统计 |
| `no-dirty-uploads` | 关闭脏区间增量上传（对照用） |
| `touch-trace` / `click-trace` | 触摸与点击诊断 |

## 8. 状态

| | Adreno（Turnip） | Mali（系统驱动） |
|---|---|---|
| 启动到菜单 | 正常 | 正常 |
| 新建角色 | 正常 | 正常 |
| 60 档 FPS | 53–55 | 38 左右 |
| 40 / 30 档 | 40.0 / 30.0 | 40.0 / 30.0 |
| 系统驱动 | 不可用（`vkCreateShaderModule` 附近崩） | 唯一可用路径 |

只在 Lenovo TB710FU（骁龙 8 Gen 3）与 vivo V2419A（天玑 9400）上验证过。
其它 SoC / 驱动组合未验证。

## 9. 没做 / 已知未解

- **Mali 在角色创建页仍报 `GROUP_ERROR_FATAL`**（见第 3 节），根因未定位。
  可疑方向：那一段纹理上传的拷贝没有按工作量分块（`sub_chunk_flush()` 只按 draw 计数），
  整段可能落进同一个超大命令缓冲区；以及上传期的池/暂存内存峰值。
- **Mali 50 fps 以上需要绘制批处理**，而 D2R 的绘制顺序约束很紧，风险高，暂缓。
- **BC 纹理始终 CPU 解码**，`DM_BC_NATIVE` 缺省关闭（打开会花屏）。
- 触屏覆盖层控件的尺寸刻度（0.62 下限）是经验值，没有做系统性的可达性测试。
