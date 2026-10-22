# 测试说明

本仓库只发布兼容层框架，**不含任何游戏文件**。因此测试分成三类：

| 层 | 需要你自己的游戏文件 | 需要 Android 设备 | 典型耗时 |
|---|---|---|---|
| **L0 主机自检** | 不需要 | 不需要 | 秒级 |
| **L1 主机 + 游戏文件** | 需要 | 不需要（Linux / WSL / Apple 芯片 Mac） | 分钟级 |
| **L2 真机** | 需要 | 需要 | 每台 5–15 分钟 |

开始之前请先读 `NOTICE.md`：仓库不提供游戏文件，你需要自己合法持有并导出。

---

## L0 主机自检（推荐先跑这一层）

### L0-a APK 打包器单元测试

不需要游戏文件、不需要设备，验证打包器对清单、路径、离线包、覆盖层的校验逻辑：

```bash
# 仓库根目录执行
PYTHONPATH=dm/tools python -m unittest discover -s dm/tools/tests -p test_android_d2r_apk.py -v
```

期望结果（本机 Windows + Python 3.12 实测）：

```
Ran 17 tests in 0.075s
OK (skipped=1)
```

`test_symlink_escape_fails` 在 Windows 上会被跳过（系统不允许创建符号链接），
在 Linux / macOS 上会真正执行。

### L0-b 双设备界面一致性（本移植新增）

同一份 APK 在不同设备上必须把菜单摆在同一位置。这个工具把两张截图各自裁到自己的
16:9 画面区、缩到 1280×720 再比较，所以不同分辨率/不同密度可以直接比：

```bash
python dm/android/tests/compare_home_layout.py 平板.png 手机.png --json result.json
```

实测输出：

```
left  pad.png   3200x2000  content 3200x1800 at 0,100  border [13, 13, 15]
right phone.png 2640x1216  content 2161x1216 at 299,0  border [13, 13, 15]
canvas 1280x720  mean diff 4.93  strong 0.99%  best shift 0px (residual 2.08)
PASS: the two devices lay the menu out the same way
```

判定阈值（可用 `--max-mean` / `--max-diff-share` 调）：

- `mean_absolute_difference ≤ 10.0`（0–255 标度）
- `strong_difference_percent ≤ 4%`（差值 > 60 的像素占比）

**它挡的是什么**：如果某台设备没有加载到触屏 UI 覆盖层，游戏会退回原版手柄界面——
角色面板摆到画面另一侧、可点的「创建新角色」变成 ✚ 键提示。实测这种状态下
`mean diff = 14.66 / strong 3.81%`，**FAIL**；修好后是 `4.93 / 0.99%`，**PASS**。
阈值就卡在这两个数之间。

`--json` 里还有 `mean_difference_by_third`（左/中/右三分区各自的差），
用来快速判断差异集中在画面哪一侧。

### L0-c 需要 Linux / WSL 的主机测试

以下几个 runner 用真实的 C / ASan / UBSan 构建并执行，不碰设备：

```bash
python dm/android/tests/run_compat_checks.py  --output out/compat  --host --vulkan-include <含 vulkan/ 的头目录>
python dm/android/tests/run_translation_checks.py --output out/translate
python dm/android/tests/run_pipeline_checks.py --help     # 以及同目录其它 run_*.py
```

它们在 Windows 上会因为缺少 `clang` + ASan 运行时不完整而失败，属预期。
各脚本的完整参数用 `--help` 看。

### L0-d 已知上游遗留失败（不作为验收项）

```bash
python dm/gfx/tests/test_tbdr_budget.py
```

当前结果：**2 passed / 3 failed**。

- 本仓库已修掉其中 2 个与移植无关的可移植性缺陷：
  `dm/gfx/tools/tbdr_budget.py` 用 `'/'` 切 `__file__` 拼路径（Windows 上切不开），
  以及几处 `open()` 未指定 UTF-8（表头是中文，Windows 默认 GBK 会解码失败）。
- 剩下 3 个是**上游内容漂移**：`texture_est_bytes` / `unknown_textures` 的期望值与工具
  输出不一致。已在纯净的上游提交 `62c6db8` 上复现，与本移植的改动无关。
  如果你要修它，改的是 `dm/gfx/tools/tbdr_budget.py` 里的预算估算，不属于本仓库范围。

---

## L1 主机 + 你自己的游戏文件

### L1-a 体检环境

```bash
./setup.sh check
```

逐项报告「状态 / 缺的话去哪儿拿 / 下一步命令」，不改动任何文件。
（这条路径面向 Apple 芯片 Mac + iOS 构建，见 `docs/DEPENDENCIES.zh-CN.md`。）

### L1-b 准备游戏文件

```bash
./setup.sh <你的游戏包>.xci <prod.keys>
```

游戏文件会落在 `$DM_ROOT/data/switch/3.3.94012/Update/{ExeFS,RomFS}`（`DM_ROOT` 缺省 `~/dm-work`）。

### L1-c 主机侧仍需游戏文件的自洽测试

```bash
python dm/host/tests/run_manifest.py <打过补丁的产物目录> <原始 main>
```

这类脚本要求工作区已经准备好（参数就是产物路径），所以放在 L1。

---

## L2 真机测试

### L2-a 编译原生库

用 Android NDK + CMake 构建共享库目标 `d2r_mobile_android`（入口见
`dm/android/CMakeLists.txt`，第 212 行附近）。同一份 CMake 工程里还挂着几十个
`EXCLUDE_FROM_ALL` 的真机探针程序（`dm_android_*_probe`），按需指定目标构建。

### L2-b 打包装机

打包器用法见 `docs/ANDROID_D2R_APK.md`：离线发布模式需要
`--offline-manifest` + `--overlay-base` + `--overlay-item-notes` + `--driver-libs` + 签名文件。
**`--overlay-base` 不能省**：

> 没有触屏 UI 覆盖层时，宿主会打 `ANDROID_CUSTOM_OVERLAY base_unavailable=1`，
> 游戏退回原版 Switch 手柄界面。表现是：角色面板摆到画面另一侧、「创建新角色」变成 ✚ 键
> 提示，于是**新建不了角色**。这就是 2026-10-22 手机与平板首页面不一致的根因。

### L2-c 真机探针

```bash
python dm/android/tests/run_compat_checks.py --output out/device --serial <adb 序列号> --run-device
```

`--run-device` 会把上面 L0-c 那批 C 测试交叉编译后推到设备上执行并回传结果。

### L2-d 真机手工验收清单

启动游戏后依次确认。括号里是应当出现的日志/现象。

**1. 触屏 UI 覆盖层生效**

```
[dm] ANDROID_CUSTOM_OVERLAY item_notes=1 root=/data/user/0/<包名>/files/d2r-mobile/romfs-overlay
[dm] 手柄 / 触屏布局：触摸说法 <N> 条（touch_strings.tsv）
[dm] 手柄 / 触屏布局：开机用触屏版（layouts/xtouch_lay 等三个目录）
```

不满足时先查同一个日志里有没有 `ANDROID_CUSTOM_OVERLAY missing=<文件名> reason=... errno=...`
（本移植加的逐文件诊断），以及 `logcat -s D2ROverlay` 里的
`APK carries no overlay-manifest.json` / `files=4 rewritten=4`。

**2. 菜单布局与另一台设备一致** → 用 L0-b 的工具比两张截图。

**3. 能新建角色**（这是最容易卡住的一条）

在新角色入口上，游戏面板里那个「创建新角色」**按钮不响应手指点击**——原版 Switch 前端
只吃手柄输入。正确的入口是屏幕右上角的 **「开始」按钮 = ✚（Plus）键**：

| 步骤 | 操作 | 期望 |
|---|---|---|
| 1 | 右上角「开始」 | 进「选择英雄职业」，8 个职业模型同屏 |
| 2 | 右下「确认」= Ⓐ | 「选择游戏模式」（标准 / 专家 + 赛季选项） |
| 3 | 再「确认」 | 「角色名称」，弹出本应用的「输入内容」输入框 |
| 4 | 输入名字 → 「确认」 | 角色建成，进入第一幕营地，血球/法力球/腰带出现 |

存档落盘确认（调试包可用 `run-as`）：

```bash
adb shell run-as <包名> ls files/d2r-mobile/save
# 期望能看到新角色目录，与已有角色并列
```

**4. 帧率档位**：系统菜单里切 30 / 40 / 60，HUD 上 FPS 应随之被压到对应上限
（60 档在 Adreno + Turnip 上实测 53–55，Mali 系统驱动上实测 38 左右，见下）。

**5. 后台返回不重载**：切出去再切回来，进程 PID 不变、画面不重新加载。
`onPause` 会释放位图池镜像；若被 `lowmemorykiller` 杀掉就说明内存又上去了。

### L2-e 自动化操作的两个坑（用 `adb shell input` 时必须知道）

1. **合成点击必须「按住」**。`input tap` 的按下-抬起只隔几毫秒，28fps 下游戏一帧都没采样到，
   表现就是「点了没反应」。用 `input swipe x y x y 400`（按住 400ms）才等效于真手指。
2. **覆盖层按钮的坐标要算，不能照截图估**。覆盖层按钮尺寸/边距都乘了
   `overlayScale = clamp(画面宽dp / 1280, 0.62, 1.0)`，
   例如手机（560dpi、画面 2161×1216）上「确认」按钮中心约在
   `画面右边界 − 39 − 74`, `画面下边界 − 39 − 63`，即 **(2347, 1114)**。
   拿截图上的视觉位置去点会落到旁边的游戏 UI 上。

---

## 本移植的实测基线（2026-10-22）

用于判断「是不是退化了」。

| 项目 | 平板 Lenovo TB710FU<br>骁龙 8 Gen 3 / Adreno 750 / Turnip | 手机 vivo V2419A<br>天玑 9400 / Mali-G925 / 系统驱动 |
|---|---|---|
| 首页面布局 | 基准 | 归一化差 4.93，位移 0px |
| 新建角色 | 正常 | 正常（走完全流程并进游戏） |
| 60 档 FPS | 53–55 | 38 左右 |
| 40 档 FPS | 40.0 | 40.0 |
| 30 档 FPS | 30.0 | 30.0 |
| 角色选择页 FPS | — | 19–20 |
| 位图优化 | `usemark_ms` 274→33.8（−88%），`pool_ms` 542→302 | — |
| 功耗（60/40/30 档） | CPU 274/236/217%，温度 84.5/70.9/62.6 °C | — |

## 已知问题与验收边界

- **Mali 上角色创建页会报 GPU 错误**：`logcat` 里可见多次
  `mali: Received a GROUP_ERROR_FATAL error on group(0)`，发生在一次性上传
  7 个职业模型纹理的那一段。本移植已让它**不再终止进程**（查询取结果失败按 0 继续，
  见 `dm/gfx/nvn/nvn_vk.c` 的 `query_read_tolerant()`），画面与流程继续，但根因未消除，
  GPU 侧仍可能掉帧或偶发异常画面。
- **Adreno 系统驱动不可用**：高通上目前只有 Turnip 能跑；选系统驱动会在
  `vkCreateShaderModule` 附近崩（约 71 帧）。`GraphicsDriverPolicy` 已按 SoC 自动选驱动。
- **BC 纹理始终 CPU 解码**：`DM_BC_NATIVE` 缺省关闭，打开会花屏。
- **L0-b 工具只验证布局一致性**，不验证颜色/局部渲染正确性；文创差异、角色数量不同、
  FPS 数字不同都会被算进 mean diff，所以阈值给得比较宽（10.0 / 4%）。
- 测试只用过两台设备（见上表）。其它 SoC / 驱动组合未验证。
