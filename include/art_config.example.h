/**
 * @file art_config.example.h
 * @brief 配置模板 —— 复制为 art_config.h 后填写你的信息
 *
 * 用法:
 *   cp include/art_config.example.h include/art_config.h
 * 然后编辑 art_config.h 填入你的 WiFi 和画廊配置。
 *
 * 注意: art_config.h 已加入 .gitignore, 包含真实 WiFi 密码,
 *       不会被推送到 GitHub。每次全新克隆仓库后,
 *       都需要重新执行上面的复制命令。
 *
 * 修改配置后, 重新编译上传即可生效。
 */
#pragma once

/* ------------------------------------------------------------------ *
 * Wi-Fi
 * ------------------------------------------------------------------ */
#define ART_WIFI_SSID       "你的WiFi名称"
#define ART_WIFI_PASSWORD   "你的WiFi密码"

/* ------------------------------------------------------------------ *
 * 图源 (联网时)
 *
 * 如果你想要"名画"内容, 推荐使用 ART_GALLERY_JSON_URL 自建图库:
 *   把一批画作图片放到 GitHub Pages / jsDelivr / Gitee / 腾讯云 COS
 *   等国内可访问的静态托管上, 再用一个 gallery.json 列表指过去:
 *   [
 *     {"title":"千里江山图","artist":"王希孟","year":"北宋","image_url":"https://..."},
 *     {"title":"蒙娜丽莎","artist":"达·芬奇","year":"1503","image_url":"https://..."}
 *   ]
 *   设备每次随机挑一条下载图片。
 *
 * 本项目自带一个"每日画作 bot" (GitHub Actions), 每天自动挑一幅
 * Marianne North 画作并通过 jsDelivr CDN 发布。启用方法:
 *   1. 把整个工程推到 GitHub (仓库需为 public)
 *   2. 仓库里 Actions 会自动每天 08:10 (北京时间) 运行
 *   3. 把下面 URL 里的 "你的用户名/仓库名" 替换后填入即可:
 *     #define ART_GALLERY_JSON_URL "https://cdn.jsdelivr.net/gh/你的用户名/仓库名@main/output/gallery.json"
 * 手动立即换画: 仓库 Actions 页面点 "Run workflow"。
 * ------------------------------------------------------------------ */
#define ART_GALLERY_JSON_URL    "https://marianne-north-art-1474440715.cos.ap-shanghai.myqcloud.com/gallery.json"

/* ------------------------------------------------------------------ *
 * IMU 左右倾斜手势
 *
 * ART_IMU_PLANE: 0 = 设备平放在桌面上; 1 = 设备竖立/靠在支架上(推荐)
 * ART_TILT_TRIGGER_DEG: 倾斜触发角度。想"几乎90度才翻页"设 75~80,
 *                      想要手感灵敏设 45 左右, 默认 60。
 * ART_TILT_RELEASE_DEG: 回正到多少度内才允许再次触发。
 * ART_TILT_HOLD_MS:     倾斜保持该时间后才触发, 防误触。
 * ART_TILT_COOLDOWN_MS: 触发后的冷却时间, 防连发。
 * ART_TILT_SIGN_INVERT: 实际使用中左右反了, 把它改成 1。
 * ------------------------------------------------------------------ */
#define ART_IMU_PLANE          1
#define ART_TILT_TRIGGER_DEG   60
#define ART_TILT_RELEASE_DEG   20
#define ART_TILT_HOLD_MS       120
#define ART_TILT_COOLDOWN_MS   400
#define ART_TILT_SIGN_INVERT   0

/* ------------------------------------------------------------------ *
 * 播放
 * ------------------------------------------------------------------ */
/* 自动轮播已关闭, 仅使用触摸翻页 */
#define ART_SLIDESHOW_MS        0UL
/* "上一张"可回退的深度 */
#define ART_HISTORY_SIZE        5
/* 单张在线图片下载上限(字节) */
#define ART_MAX_IMAGE_BYTES     (1536UL * 1024UL)
/* HTTP 请求超时(ms) */
#define ART_HTTP_TIMEOUT_MS     10000
/* 解码/显示的单张图片最大边长(像素), 超出则提示不显示 */
#define ART_MAX_DECODE_WH       1280

/* ------------------------------------------------------------------ *
 * SD 卡图库
 *
 * 目录结构:
 *   /art/xxx.jpg         画作 (jpg/jpeg 均可)
 *   /art/xxx.json        可选元数据: {"title":"...","artist":"...","year":"..."}
 *   /art/cache/          联网图片的自动缓存目录 (勿手动改动)
 * ------------------------------------------------------------------ */
#define ART_SD_ROOT             "/art"
#define ART_SD_CACHE            "/art/cache"
#define ART_SD_MAX_FILES        600

/* ------------------------------------------------------------------ *
 * 屏幕
 * ------------------------------------------------------------------ */
#define ART_SCREEN_ROTATION     1   /* 1 或 3 为横屏 */
