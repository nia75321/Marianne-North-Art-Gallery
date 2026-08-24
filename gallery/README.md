# Marianne North 图库

本目录存放 Marianne North（玛丽安娜·诺斯，1830–1890）画作图库。
她的画作藏于英国皇家植物园邱园的 Marianne North Gallery，均为公有领域作品。

## 目录结构

```
gallery/
├── gallery.json        # 在线模式图库清单 (ART_GALLERY_JSON_URL 使用)
├── images/             # 批量下载的图片 + SD 卡元数据 (运行脚本生成)
└── README.md
```

## 方案对比

| 方案 | 说明 | 推荐度 |
| ---- | ---- | ---- |
| **每日画作 bot** | GitHub Actions 每天自动挑一幅, jsDelivr CDN 下发, 每天一换 | ★★★★★ |
| 离线 SD 卡 | 批量下载后拷到 SD 卡 `/art/`, 完全离线 | ★★★★ |
| 自建在线图库 | 图片托管到国内 CDN + gallery.json 清单 | ★★★ |

## 方案一：每日画作 bot（推荐）

设备每天自动更新一幅 Marianne North 画作，实现"每日一画"。

原理：
```
[Wikimedia] --每天 08:10--> [GitHub Actions bot] --推送到--> [你的 GitHub 仓库]
    设备 <--jsDelivr CDN (国内节点)-- 拉取 output/gallery.json + 图片
```

步骤：

1. **把工程推送到 GitHub**（仓库必须为 public，jsDelivr 才能读取）
2. **确认 Actions 已启用**（个人仓库默认启用）
3. **每天北京时间 08:10** bot 自动挑一幅新画作并提交
4. 在 `include/art_config.h` 中配置：

```c
#define ART_GALLERY_JSON_URL "https://cdn.jsdelivr.net/gh/你的用户名/仓库名@main/output/gallery.json"
```

5. 想立刻换画：GitHub 仓库 → Actions → Daily Painting → Run workflow

bot 的文件：
- `.github/workflows/daily-painting.yml` — 定时任务
- `tools/daily_painting.py` — bot 逻辑
- `output/gallery.json` — 设备读取的清单（最近 7 天画作）
- `output/painting-YYYYMMDD.jpg` — 每日画作图片

## 方案二：离线 SD 卡模式

### 1. 下载图片（需要能访问 Wikimedia 的网络）

```bash
# 全部下载 (约 800 张)
python3 tools/download_marianne_north.py

# 只下载前 60 张试试
python3 tools/download_marianne_north.py --limit 60

# 国内网络需走代理时
HTTPS_PROXY=http://127.0.0.1:7890 python3 tools/download_marianne_north.py
```

### 2. 拷贝到 SD 卡

把 `gallery/images/*.jpg` 和 `*.json` 全部拷贝到 SD 卡 `/art/` 目录，
固件会自动随机播放，不依赖网络。

## 方案三：在线模式（自建图库）

1. 把 `gallery/images/` 里的图片托管到国内可访问的静态服务
   （GitHub Pages / jsDelivr / 腾讯云 COS / 阿里云 OSS）
2. 生成图库清单：

```bash
python3 tools/download_marianne_north.py --generate-json --base-url https://cdn.example.com/images
```

3. 在 `include/art_config.h` 中配置：

```c
#define ART_GALLERY_JSON_URL "https://cdn.example.com/gallery.json"
```

## 版权说明

Marianne North 于 1890 年去世，作品均已进入公有领域（PD-old-100）。
图片来源为 Wikimedia Commons "Category:Paintings by Marianne North"。

