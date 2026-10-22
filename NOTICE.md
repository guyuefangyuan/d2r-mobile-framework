# 仓库范围与版权声明 / Scope and copyright notice

## 这个仓库是什么

`d2r-mobile` 是一层 **兼容层（compatibility framework）**：它把 Nintendo Switch 版游戏
原本调用的图形/系统接口（NVN、HID、文件系统等）翻译到 Android 上（Vulkan + JNI），
使得在**你自己合法持有游戏文件**的前提下，可以在 Android 设备上运行。

本仓库只发布**框架源码**：C / Objective-C / Java / Python 源码、构建脚本、测试用例与文档。

## 这个仓库里没有什么（很重要）

以下内容**不在**本仓库，也**不会**提供，请不要在 issue / PR 里索取：

- 游戏的任何数据或代码：`game.romfs`、`main` / `sdk` / `subsdk*` NSO、shader 包、纹理、音频、
  模型、UI 布局、字体、`*.nsp` / `*.xci`、`prod.keys` / `title.keys`；
- 由上述内容派生出的资源：解包后的美术资源、HUD 图集、改写过的游戏数据包
  （例如 `data/00-00000007.data`）、补丁后的模块镜像；
- 打包好的 APK / IPA / 任何可分发二进制；
- 任何帮助你获取游戏文件的工具、链接、种子或密钥。

仓库里**唯一**与游戏运行观测有关的是几张 TSV 表（`dm/gfx/nvn/shader_map.tsv`、
`shader_facts.tsv`、`pso_manifest.tsv`）。它们记录的是「着色器哈希 → 用到的资源类型」
这类**事实型观测**，由仓库内 `dm/gfx/tools/` 的脚本对作者自己持有的游戏副本生成，
不包含游戏代码或美术资源。你可以用自己的一份重新生成。

## 你需要自备

- 一份合法购买的 Nintendo Switch 版 *Diablo II: Resurrected*（含更新补丁），
  以及你自己从主机导出的密钥；
- 提取与准备工作区的方法见 `setup.sh`、`docs/DEPENDENCIES.zh-CN.md`。

本仓库**不提供**获取游戏文件的途径，也不对如何获取提供建议。

## 商标与从属关系

*Diablo II: Resurrected* 及相关名称、美术、音频是 Blizzard Entertainment, Inc. 的商标
和/或版权财产。*Nintendo Switch* 是任天堂的商标。本仓库与上述公司**没有任何从属、
赞助或背书关系**。仓库中出现的游戏名称仅用于说明用途（描述兼容目标），属于指称性使用。

本仓库**不包含**任何 Blizzard 或任天堂的美术资源：启动图标（Android `mipmap-*`、
`drawable-nodpi/ic_launcher_foreground.png`、iOS `AppIcon1024.png`）是由
`dm/android/tools/gen_neutral_icon.py` 用几何图形现场生成的原创占位图，
可由你随意替换成自己的品牌图。

## 许可

- 本仓库基于 [dodola/d2r-mobile](https://github.com/dodola/d2r-mobile)（MIT），
  保留其 `LICENSE` 与版权声明；
- 本仓库的修改同样以 **MIT** 发布，见 `LICENSE`；
- `dm/third_party/` 下的第三方组件各自遵循其原有许可（Ryujinx：MIT；libdeflate：MIT；
  tinyexr / stb：MIT 或 Public Domain），详见各自目录内的许可文件。

## 免责

本软件按「现状」提供，不附带任何担保。你需要自行确认在你所在司法辖区内的行为合法性，
并自行承担使用风险。请勿将本仓库用于分发游戏本体或任何受版权保护的内容。
