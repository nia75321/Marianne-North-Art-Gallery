#!/usr/bin/env python3
"""
Daily Painting Bot —— 每日画作机器人

每天从 Wikimedia Commons 的 "Category:Paintings by Marianne North"
随机挑选一幅画作, 下载 800px 缩略图, 生成 gallery.json + 图片文件,
供设备通过 jsDelivr CDN 拉取(中国大陆可访问)。

通常在 GitHub Actions 里每天定时运行, 也可以本地手动运行。

依赖: 仅 Python 标准库。需要能访问 Wikimedia 的网络(可用代理)。
"""

import html
import json
import os
import random
import re
import ssl
import sys
import time
import urllib.parse
import urllib.request

API = "https://commons.wikimedia.org/w/api.php"
CATEGORY = "Category:Paintings by Marianne North"
# COS 可稳定承载较大图片, 800px 用于设备放大查看细节
WIDTH = 800
OUT_DIR = "output"
ARTIST = "Marianne North"
USER_AGENT = "MarianneNorthDailyBot/1.0 (GitHub Actions; personal art gallery)"

HISTORY_FILE = os.path.join(OUT_DIR, "history.json")
GALLERY_FILE = os.path.join(OUT_DIR, "gallery.json")
KEEP_RECENT = 7      # gallery.json 保留最近 7 天
KEEP_FILES = 12      # 磁盘上保留最近 12 张图片
HISTORY_LEN = 300    # history.json 记住最近 300 条, 防止短期内重复


# ---------------------------------------------------------------------------
# Wikimedia API
# ---------------------------------------------------------------------------

def api_request(params):
    """调用 Wikimedia API, 失败重试 4 次"""
    url = API + "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, context=ctx, timeout=30) as r:
                return json.loads(r.read().decode("utf-8"))
        except Exception as e:
            if attempt == 3:
                print(f"  ! API 请求失败: {e}")
                return None
            time.sleep(3)
    return None


def fetch_all_titles():
    """分页获取分类下全部画作文件名"""
    titles = []
    cmcontinue = None
    while True:
        params = {
            "action": "query",
            "list": "categorymembers",
            "cmtitle": CATEGORY,
            "cmtype": "file",
            "cmlimit": "500",
            "format": "json",
        }
        if cmcontinue:
            params["cmcontinue"] = cmcontinue
        data = api_request(params)
        if not data:
            break
        for m in data.get("query", {}).get("categorymembers", []):
            t = m.get("title", "")
            if t.lower().endswith(".jpg") and "Thismia" not in t:
                titles.append(t)
        cont = data.get("continue", {})
        cmcontinue = cont.get("cmcontinue")
        if not cmcontinue:
            break
    return titles


def fetch_file_info(title):
    """获取单个文件的 800px 缩略图 URL 与元数据"""
    params = {
        "action": "query",
        "titles": title,
        "prop": "imageinfo",
        "iiprop": "url|size|mime|extmetadata",
        "iiurlwidth": str(WIDTH),
        "format": "json",
        "formatversion": "2",
    }
    data = api_request(params)
    if not data:
        return None
    pages = data.get("query", {}).get("pages", [])
    if not pages:
        return None
    p = pages[0]
    ii = p.get("imageinfo") or []
    if not ii:
        return None
    info = ii[0]
    thumburl = info.get("thumburl") or info.get("url") or ""
    meta = info.get("extmetadata", {})
    return {
        "thumburl": thumburl,
        "object": meta.get("ObjectName", {}).get("value", ""),
        "year_raw": meta.get("DateTimeOriginal", {}).get("value", ""),
    }


# ---------------------------------------------------------------------------
# 文本处理
# ---------------------------------------------------------------------------

def strip_html(s):
    if not s:
        return ""
    clean = re.sub(r"<[^>]+>", "", s)
    return html.unescape(clean).strip()


def extract_year(raw):
    """从 DateTimeOriginal 提取年份, 有 circa/about 前缀时标 c."""
    clean = strip_html(raw)
    m2 = re.search(r"\b(1[89]\d{2})\b", clean)
    if not m2:
        return ""
    year = m2.group(1)
    before = clean[:m2.start()]
    if re.search(r"circa|about|c\.?\s*$", before, re.IGNORECASE):
        return f"c.{year}"
    return year


def extract_object_title(object_html):
    """从 ObjectName 的 HTML 里提取干净标题。

    Commons 的 ObjectName 形如:
      <div class="fn"><div ...><i>画作标题</i></div><div style="display:none">
      label QS:Len,"画作标题"</div></div>
    优先取 <i> 标签内容; 没有则去掉 label QS 垃圾文本后返回。
    """
    if not object_html:
        return ""
    m = re.search(r"<i[^>]*>(.*?)</i>", object_html, re.DOTALL)
    if m:
        return html.unescape(re.sub(r"<[^>]+>", "", m.group(1))).strip()
    clean = re.sub(r"<[^>]+>", "", object_html)
    clean = html.unescape(clean)
    clean = re.sub(r"label\s*QS:[^\"]*", "", clean)
    return clean.strip()


def parse_title(filename):
    """从文件名解析画作标题"""
    if filename.startswith("File:"):
        filename = filename[5:]
    name = os.path.splitext(filename)[0]

    m = re.match(r"Marianne North\s*\(1830-1890\)\s*-\s*(.*?)\s*-\s*MN\d+\s*-?\s*Marianne North Gallery.*", name)
    if m:
        return m.group(1).strip()

    m = re.match(r"MN\d+\s+(.*)", name)
    if m:
        return m.group(1).strip()

    m = re.match(r"Marianne North\s*-\s*(.*)", name)
    if m:
        return m.group(1).strip()

    m = re.match(r"Marianne North\s+(.*)", name)
    if m and len(m.group(1)) > 5:
        return m.group(1).strip()

    name = re.sub(r"^Marianne North[\s\-]*", "", name).strip()
    return name if name else "Untitled"


# ---------------------------------------------------------------------------
# 输出
# ---------------------------------------------------------------------------

def download(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    try:
        with urllib.request.urlopen(req, context=ctx, timeout=60) as r:
            data = r.read()
            if len(data) < 1000:
                return False
            with open(path, "wb") as f:
                f.write(data)
        return True
    except Exception as e:
        print(f"  ! 图片下载失败: {e}")
        return False


def load_history():
    if os.path.exists(HISTORY_FILE):
        try:
            with open(HISTORY_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return []


def save_history(history):
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(HISTORY_FILE, "w", encoding="utf-8") as f:
        json.dump(history, f, ensure_ascii=False, indent=2)


def main():
    repo = os.environ.get("REPO", "").strip()
    branch = os.environ.get("BRANCH", "main").strip()
    if not repo:
        print("! 警告: 未检测到 REPO 环境变量 (在 GitHub Actions 中自动设置),")
        print("! 生成的 gallery.json 将使用占位地址, 仅可在本地测试流程使用。")

    # 1. 获取全部画作
    print("[1/4] 获取 Marianne North 画作列表...")
    titles = fetch_all_titles()
    if not titles:
        print("! 获取画作列表失败 (Wikimedia 不可达? 请检查网络/代理)")
        sys.exit(1)
    print(f"  ✓ 共 {len(titles)} 幅")

    # 2. 挑选一幅没展示过的
    print("[2/4] 挑选今日画作...")
    history = load_history()
    used = {h["file"] for h in history}
    candidates = [t for t in titles if t not in used]
    if not candidates:
        candidates = titles  # 全部展示过一轮了, 重新开始
    pick = random.choice(candidates)
    print(f"  ✓ 选中: {pick}")

    # 3. 获取元数据 + 下载图片
    print("[3/4] 获取元数据并下载图片...")
    info = fetch_file_info(pick)
    if not info or not info["thumburl"]:
        print("! 获取图片信息失败")
        sys.exit(1)

    title = parse_title(pick)
    obj_title = extract_object_title(info["object"])
    if len(obj_title) > 2:
        title = obj_title
    year = extract_year(info["year_raw"])
    print(f"  ✓ {title} ({year})")

    today = time.strftime("%Y%m%d")
    img_name = f"painting-{today}.jpg"
    img_path = os.path.join(OUT_DIR, img_name)

    # 同一天重复运行直接覆盖
    history = [h for h in history if h["date"] != today]
    if not download(info["thumburl"], img_path):
        print("! 图片下载失败")
        sys.exit(1)
    print(f"  ✓ 已保存 {img_path}")

    # 4. 写 history.json + gallery.json
    print("[4/4] 生成 gallery.json...")
    history.insert(0, {"date": today, "file": pick, "title": title, "year": year})
    history = history[:HISTORY_LEN]
    save_history(history)

    items = []
    for h in history[:KEEP_RECENT]:
        items.append({
            "title": h["title"],
            "artist": ARTIST,
            "year": h["year"],
            "image_url": f"painting-{h['date']}.jpg",
        })
    with open(GALLERY_FILE, "w", encoding="utf-8") as f:
        json.dump(items, f, ensure_ascii=False, indent=2)
    print(f"  ✓ 已生成 {GALLERY_FILE} ({len(items)} 条)")

    # 5. 清理旧图片
    existing = [f for f in os.listdir(OUT_DIR) if re.match(r"painting-\d{8}\.jpg$", f)]
    existing.sort(reverse=True)
    for f in existing[KEEP_FILES:]:
        os.remove(os.path.join(OUT_DIR, f))
        print(f"  - 清理旧图: {f}")

    print("\n完成!")


if __name__ == "__main__":
    main()