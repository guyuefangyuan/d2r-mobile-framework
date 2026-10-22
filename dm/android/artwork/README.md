# Launcher icon

这里**不再**附带任何来自游戏的图像。仓库里所有启动图标都是
`dm/android/tools/gen_neutral_icon.py` 用几何图形现场画出来的中性占位图：

| 文件 | 用途 |
|---|---|
| `artwork/app-icon.png` | 512×512 母版，方便替换成自己的品牌图 |
| `res/drawable-nodpi/ic_launcher_foreground.png` | 自适应图标前景（432×432，图形落在中央 60% 安全区内） |
| `res/mipmap-{m,h,x,xx,xxx}dpi/ic_launcher.png` | 传统图标，48 / 72 / 96 / 144 / 192 |
| `dm/ios/Assets.xcassets/AppIcon.appiconset/AppIcon1024.png` | iOS，1024×1024 |

重新生成（只依赖 Pillow）：

```bash
python dm/android/tools/gen_neutral_icon.py
```

要换成自己的图标，直接覆盖上面这些文件即可（保持文件名与尺寸），或者改脚本开头的
`BG` / `GOLD` / `RIM` 三个颜色后再跑一次。

> 说明：上游版本曾在这里放一张 AI 生成的暗黑风头盔母版；本仓库发布时已删除并改成
> 中性图形，以免与任何既有游戏的美术混淆。见仓库根目录 `NOTICE.md`。
