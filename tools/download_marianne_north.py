#!/usr/bin/env python3
"""
Marianne North 画作下载器
从 Wikimedia Commons 抓取全部画作的元数据 + 图片，生成 SD 卡图库和在线图库清单。

用法:
  python3 tools/download_marianne_north.py                    # 全部下载
  python3 tools/download_marianne_north.py --limit 60          # 只下载 60 张
  python3 tools/download_marianne_north.py --generate-json     # 只生成 gallery.json 不下载
  python3 tools/download_marianne_north.py --base-url "https://cdn.example.com/images"  # 自定义图片前缀
  HTTPS_PROXY=http://127.0.0.1:7890 python3 tools/download_marianne_north.py  # 走代理
"""

import argparse
import html
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
import ssl

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------
API = "https://commons.wikimedia.org/w/api.php"
CATEGORY = "Category:Paintings by Marianne North"
WIDTH = 800
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "gallery", "images")
ARTIST = "Marianne North"
USER_AGENT = "MarianneNorthGallery/1.0 (personal art gallery; https://github.com/user/M5STACK_coreS3_camera)"

# 跳过非画作文件（真菌照片等）
SKIP_KEYWORDS = ["Thismia neptunis", "Panoramix", "thumbnail"]

# 单张图片下载超时（秒）
DOWNLOAD_TIMEOUT = 30

# api 请求重试
MAX_RETRIES = 3

# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def make_ssl_ctx():
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx

def api_request(params, retries=MAX_RETRIES):
    """调用 Wikimedia API，返回 JSON 对象"""
    url = API + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, context=make_ssl_ctx(), timeout=30) as r:
                return json.loads(r.read().decode("utf-8"))
        except (urllib.error.URLError, socket.timeout, OSError) as e:
            if attempt == retries - 1:
                print(f"  API 请求失败: {e}")
                return None
            time.sleep(2)
    return None

def strip_html(s):
    """去掉 HTML 标签"""
    if not s:
        return ""
    clean = re.sub(r"<[^>]+>", "", s)
    clean = html.unescape(clean)
    clean = clean.strip()
    return clean

def sanitize_filename(s):
    """将文件名清理为安全的短文件名"""
    s = re.sub(r"[^a-zA-Z0-9\u4e00-\u9fff\-_ ]", "", s)
    s = re.sub(r"\s+", " ", s).strip()
    if len(s) > 80:
        s = s[:80]
    if not s:
        s = "untitled"
    return s

def parse_title_and_num(filename):
    """从文件名解析画作标题，去掉 Marianne North 前缀和编号后缀"""
    # 去掉 "File:" 前缀
    if filename.startswith("File:"):
        filename = filename[5:]
    # 去掉扩展名
    name = os.path.splitext(filename)[0]

    # 模式1: Marianne North (1830-1890) - <TITLE> - MN<num> - Marianne North Gallery[,...]
    m = re.match(r"Marianne North\s*\(1830-1890\)\s*-\s*(.*?)\s*-\s*MN\d+\s*-?\s*Marianne North Gallery.*", name)
    if m:
        return m.group(1).strip()

    # 模式2: MN<num> <TITLE>
    m = re.match(r"MN\d+\s+(.*)", name)
    if m:
        return m.group(1).strip()

    # 模式3: Marianne North - <TITLE>
    m = re.match(r"Marianne North\s*-\s*(.*)", name)
    if m:
        return m.group(1).strip()

    # 模式4: Marianne North <TITLE> (无分隔符)
    m = re.match(r"Marianne North\s+(.*)", name)
    if m and len(m.group(1)) > 5:
        return m.group(1).strip()

    # 模式5: 其他，直接返回清理后的文件名
    # 去掉 "Marianne North" 残留
    name = re.sub(r"^Marianne North[\s\-]*", "", name).strip()
    return name if name else "Untitled"

# ---------------------------------------------------------------------------
# 核心功能
# ---------------------------------------------------------------------------

def fetch_all_files():
    """获取该分类下所有画作文件列表（含 pageid 和 title）"""
    files = []
    gcmcontinue = None
    page = 0

    while True:
        params = {
            "action": "query",
            "generator": "categorymembers",
            "gcmtitle": CATEGORY,
            "gcmnamespace": "6",
            "gcmtype": "file",
            "gcmlimit": "500",
            "prop": "imageinfo",
            "iiprop": "url|size|mime",
            "format": "json",
            "formatversion": "2",
        }
        if gcmcontinue:
            params["gcmcontinue"] = gcmcontinue

        data = api_request(params)
        if not data:
            break

        pages = data.get("query", {}).get("pages", {})
        # pages 是对象时 (formatversion=2 -> array)
        if isinstance(pages, list):
            for p in pages:
                title = p.get("title", "")
                pageid = p.get("pageid", 0)
                # 跳过非画作
                skip = any(kw in title for kw in SKIP_KEYWORDS)
                if not skip and title.lower().endswith(".jpg"):
                    files.append({"pageid": pageid, "title": title})
        else:
            # 旧版格式: dict keyed by pageid
            for pageid, p in pages.items():
                title = p.get("title", "")
                skip = any(kw in title for kw in SKIP_KEYWORDS)
                if not skip and title.lower().endswith(".jpg"):
                    files.append({"pageid": int(pageid), "title": title})

        # 继续
        cont = data.get("continue", {})
        gcmcontinue = cont.get("gcmcontinue")
        if not gcmcontinue:
            break

        page += 1
        print(f"  已获取 {len(files)} 个文件...")

    return files


def fetch_years(pageids):
    """批量获取画作的年份 (DateTimeOriginal)"""
    years = {}
    # 每 50 个一批
    for i in range(0, len(pageids), 50):
        batch = pageids[i:i+50]
        params = {
            "action": "query",
            "pageids": "|".join(str(p) for p in batch),
            "prop": "imageinfo",
            "iiprop": "extmetadata",
            "format": "json",
            "formatversion": "2",
        }
        data = api_request(params)
        if not data:
            continue

        pages = data.get("query", {}).get("pages", [])
        if isinstance(pages, list):
            for p in pages:
                pi = p.get("pageid", 0)
                ii = p.get("imageinfo", [])
                if ii:
                    meta = ii[0].get("extmetadata", {})
                    raw = meta.get("DateTimeOriginal", {}).get("value", "")
                    years[pi] = extract_year(raw)
        else:
            for pageid, p in pages.items():
                pi = int(pageid)
                ii = p.get("imageinfo", [])
                if ii:
                    meta = ii[0].get("extmetadata", {})
                    raw = meta.get("DateTimeOriginal", {}).get("value", "")
                    years[pi] = extract_year(raw)

        time.sleep(0.5)
        print(f"  已获取 {len(years)} 个年份...")

    return years


def extract_year(raw):
    """从 DateTimeOriginal 字符串中提取年份"""
    if not raw:
        return ""
    clean = strip_html(raw)
    # 尝试匹配 4 位数字年份
    m = re.search(r"\b(1[89]\d{2})\b", clean)
    if m:
        return m.group(1)
    # 尝试匹配 "circa YYYY"
    m = re.search(r"(?:circa|c\.|约)\s*(1[89]\d{2})", clean, re.IGNORECASE)
    if m:
        return f"c.{m.group(1)}"
    return ""


def download_image(url, path):
    """下载图片到本地文件"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, context=make_ssl_ctx(), timeout=DOWNLOAD_TIMEOUT) as r:
            data = r.read()
            with open(path, "wb") as f:
                f.write(data)
        return True
    except Exception as e:
        print(f"  ! 下载失败: {url[:80]}... → {e}")
        return False


def write_sd_metadata(path, title, year):
    """为 SD 卡模式写入 json 元数据"""
    meta = {"title": title, "artist": ARTIST, "year": year}
    base = os.path.splitext(path)[0]
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False)


def generate_gallery_json(entries, base_url):
    """生成 gallery.json 清单"""
    items = []
    for e in entries:
        items.append({
            "title": e["title"],
            "artist": ARTIST,
            "year": e["year"],
            "image_url": base_url + "/" + e["sanitized"] + ".jpg",
        })
    out_path = os.path.join(os.path.dirname(__file__), "..", "gallery", "gallery.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(items, f, ensure_ascii=False, indent=2)
    print(f"✓ 已生成 {out_path} ({len(items)} 条)")
    return items


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Marianne North 画作下载器")
    parser.add_argument("--limit", type=int, default=0, help="下载数量上限")
    parser.add_argument("--generate-json", action="store_true", help="仅生成 gallery.json 不下载图片")
    parser.add_argument("--base-url", default="https://commons.wikimedia.org/wiki/Special:FilePath",
                        help="图库在线访问的 base URL")
    parser.add_argument("--no-download", action="store_true", help="不下载图片，只获取元数据")
    args = parser.parse_args()

    print("=" * 60)
    print("Marianne North 画作下载器")
    print("=" * 60)

    # 1. 获取文件列表
    print("\n[1/4] 获取画作列表...")
    import socket
    files = fetch_all_files()
    if not files:
        print("! 获取文件列表失败，请检查网络连接")
        print("  提示: 如果在中国大陆，尝试设置 HTTPS_PROXY 环境变量")
        sys.exit(1)
    print(f"  ✓ 共 {len(files)} 个画作文件")

    # 2. 获取年份
    print("\n[2/4] 获取画作年份...")
    pageids = [f["pageid"] for f in files]
    years = fetch_years(pageids)

    # 3. 构建条目
    entries = []
    for f in files:
        title = parse_title_and_num(f["title"])
        year = years.get(f["pageid"], "")
        # 用 MN 编号或 pageid 作为标识
        m = re.search(r"MN(\d+)", f["title"])
        ident = f"MN{m.group(1)}" if m else f"page{f['pageid']}"
        sanitized = sanitize_filename(f"{ident}_{title}")
        entries.append({
            "title": title,
            "year": year,
            "filename": f["title"],
            "sanitized": sanitized,
            "pageid": f["pageid"],
        })
        if args.limit and len(entries) >= args.limit:
            break

    print(f"  ✓ 整理完毕 {len(entries)} 条")

    if args.generate_json:
        # 仅生成 gallery.json
        print("\n[3/4] 生成 gallery.json...")
        generate_gallery_json(entries, args.base_url)
        print("\n  ✓ 完成！")
        return

    # 4. 下载图片
    print(f"\n[3/4] 下载图片 (最多 {len(entries)} 张)...")
    os.makedirs(OUT_DIR, exist_ok=True)
    downloaded = 0
    failed = 0

    for i, e in enumerate(entries):
        special_url = f"https://commons.wikimedia.org/wiki/Special:FilePath/{urllib.parse.quote(e['filename'])}?width={WIDTH}"
        # 本地文件路径
        jpg_path = os.path.join(OUT_DIR, f"{e['sanitized']}.jpg")

        if os.path.exists(jpg_path) and os.path.getsize(jpg_path) > 1000:
            # 已有文件则跳过
            print(f"  [{i+1}/{len(entries)}] 跳过 (已存在): {e['title'][:50]}")
            # 确保 json 存在
            write_sd_metadata(jpg_path, e["title"], e["year"])
            downloaded += 1
            continue

        print(f"  [{i+1}/{len(entries)}] {e['title'][:60]}...", end=" ", flush=True)
        success = download_image(special_url, jpg_path)
        if success:
            write_sd_metadata(jpg_path, e["title"], e["year"])
            downloaded += 1
            print("✓")
        else:
            failed += 1
            print("✗")

        # 每 10 张休息一下，避免被限流
        if (i + 1) % 10 == 0:
            time.sleep(1)

    print(f"\n  ✓ 下载完成: {downloaded} 成功, {failed} 失败")

    # 5. 生成 gallery.json
    print(f"\n[4/4] 生成 gallery.json...")
    generate_gallery_json(entries, args.base_url)

    print(f"\n{'=' * 60}")
    print(f"完成！图片已保存到: {OUT_DIR}")
    print(f"gallery.json 已生成")
    print(f"\nSD 卡模式: 将 {OUT_DIR}/*.jpg + *.json 拷贝到 /art/")
    print(f"在线模式: 设置 ART_GALLERY_JSON_URL 指向 gallery.json")
    print(f"{'=' * 60}")


if __name__ == "__main__":
    main()