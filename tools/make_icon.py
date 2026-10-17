"""生成应用图标 app.ico —— 与 makeTrayIcon() 同一套设计。

设计（见 mainwindow.cpp 的 makeTrayIcon）：
  深色圆角底 + 青绿(#00FFC8)描边 + 青绿对勾
颜色故意写死而不是取主题：应用图标要在资源管理器/开始菜单里显示，
那些地方没有主题上下文，必须固定。
"""
from PIL import Image, ImageDraw

BG = (16, 16, 28)
TEAL = (0, 255, 200)


def render(size: int) -> Image.Image:
    # 4x 超采样再缩小，得到平滑边缘（PIL 的圆角/线段不做抗锯齿）
    ss = 4
    s = size * ss
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    r = s * 0.22
    lw = max(1, round(s / 16))
    d.rounded_rectangle([lw / 2, lw / 2, s - 1 - lw / 2, s - 1 - lw / 2],
                        radius=r, fill=BG, outline=TEAL, width=lw)

    # 对勾：与 makeTrayIcon 里的三点一致
    pts = [(s * 0.26, s * 0.52), (s * 0.44, s * 0.70), (s * 0.76, s * 0.32)]
    w = max(2, round(s / 8))
    d.line(pts, fill=TEAL, width=w, joint="curve")
    # 补两个端点圆头（PIL 的 line 没有 round cap）
    for (x, y) in (pts[0], pts[-1]):
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=TEAL)

    return img.resize((size, size), Image.LANCZOS)


sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [render(n) for n in sizes]
# 用最大那张作为基准，PIL 会写入全部尺寸
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------- Windows .ico
OUT = ROOT / "resources" / "app.ico"
OUT.parent.mkdir(parents=True, exist_ok=True)
imgs[-1].save(OUT, format="ICO",
              sizes=[(n, n) for n in sizes])
print("已生成 resources/app.ico")
for n in sizes:
    print(f"  {n}x{n}")

# ---------------------------------------------------------------- macOS .icns
#
# 为什么在这里生成而不是在 CI 里用 sips 从 .ico 转：
# **sips 不支持 ICO 格式**（它是 macOS 自带工具，但不认 Windows 的 .ico），
# CI 上 `sips -z 16 16 app.ico --out icon_16x16.png` 直接报错，
# 整个 macOS job 卡在这一步。
#
# 与其在 CI 里想办法解 ICO，不如在这里直接产出 .iconset —— 反正图案的
# 源头就是这个脚本，从这里出图质量最好也最省事。
# iconutil（macOS 自带）只接受这个目录结构和这套文件名。
if "--icns" in sys.argv:
    iconset = ROOT / "app.iconset"
    iconset.mkdir(parents=True, exist_ok=True)
    # iconutil 要求的固定命名：成对出现 <n>x<n>.png 与 <n>x<n>@2x.png
    for n in (16, 32, 128, 256, 512):
        render(n).save(iconset / f"icon_{n}x{n}.png")
        render(n * 2).save(iconset / f"icon_{n}x{n}@2x.png")
    print(f"已生成 {iconset}")
    for f in sorted(iconset.iterdir()):
        print(f"  {f.name}")

