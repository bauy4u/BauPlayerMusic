#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import re
import time
from pathlib import Path

import requests


DEFAULT_SEED = """
的一是在不了有和人这中大为上个国我以要他时来用们生到作地于出就分对成会可主发年动同工也能下过子说产种面而方后多定行学法所民得经十三之进着等部度家电力里如水化高自二理起小物现实加量都两体制机当使点从业本去把性好应开它合还因由其些然前外天政四日那社义事平形相全表间样与关各重新线内数正心反你明看原又么利比或但质气第向道命此变条只没结解问意建月公无系军很情者最立代想已通并提直题党程展五果料象员革位入常文总次品式活设及管特件长求老头基资边流路级少图山统接知较将组见计别她手角期根论运农指几九区强放决西被干做必战先回则任取据处队南给色光门即保治北造百规热领七海口东导器压志世金增争济阶油思术极交受联什认六共权收证改清美再采转更单风切打白教速花带安场身车例真务具万每目至达走积示议声报斗完类八离华名确才科张信马节话米整空元况今集温传土许步群广石记需段研界拉林律叫且究观越织装影算低持音众书布复容儿须际商非验连断深难近矿千周委素技备半办青省列习响约支般史感劳便团往酸历市克何除消构府称太准精值号率族维划选标写存候毛亲快效斯院查江型眼王按格养易置派层片始却专状育厂京识适属圆包火住调满县局照参红细引听该铁价严龙飞
汉字内容测试听歌服点歌音乐歌词队列播放地图海龟汤探长谁是卧底玩家服务器群消息状态排行绑定验证码今日运势猜歌房间开始结束投票发言提示真相答案
呀啊哦呢吧吗嘛啦哈嘿哎诶喂嗯哇曾经慢慢品在推着我前进
"""

DEFAULT_FREQUENCY_URL = "https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/zh_cn/zh_cn_50k.txt"


def iter_chars(text):
    seen = set()
    for char in text:
        code = ord(char)
        if 0x4E00 <= code <= 0x9FFF and char not in seen:
            seen.add(char)
            yield char


def load_extra_text(paths):
    chunks = []
    for path in paths:
        p = Path(path)
        if p.exists():
            chunks.append(p.read_text(encoding="utf-8", errors="ignore"))
    return "\n".join(chunks)


def fetch_graphics_index(timeout=90):
    url = "https://raw.githubusercontent.com/skishore/makemeahanzi/master/graphics.txt"
    response = requests.get(url, timeout=timeout)
    response.raise_for_status()
    index = {}
    for raw_line in response.text.splitlines():
        if not raw_line.strip():
            continue
        try:
            item = json.loads(raw_line)
        except json.JSONDecodeError:
            continue
        char = item.get("character")
        medians = item.get("medians")
        if isinstance(char, str) and len(char) == 1 and isinstance(medians, list):
            index[char] = medians
    return index


def fetch_frequency_text(url=DEFAULT_FREQUENCY_URL, timeout=45):
    try:
        response = requests.get(url, timeout=timeout)
        response.raise_for_status()
    except requests.RequestException as exc:
        print(f"[warn] failed to fetch zh_cn frequency words: {exc}")
        return ""

    words = []
    for raw_line in response.text.splitlines():
        if not raw_line.strip():
            continue
        words.append(raw_line.split()[0])
    return "".join(words)


def normalize_medians(medians):
    strokes = []
    for stroke in medians:
        points = []
        for point in stroke:
            if not isinstance(point, list) or len(point) < 2:
                continue
            try:
                x = int(round(float(point[0])))
                y = int(round(float(point[1])))
            except (TypeError, ValueError):
                continue
            points.append((max(0, min(1024, x)), max(0, min(1024, y))))
        if len(points) >= 2:
            strokes.append(points)
    return strokes


def encode_line(char, strokes):
    encoded_strokes = []
    for stroke in strokes:
        encoded_strokes.append(" ".join(f"{x},{y}" for x, y in stroke))
    return f"{ord(char):04X};" + "|".join(encoded_strokes)


def main():
    parser = argparse.ArgumentParser(description="Fetch Hanzi Writer medians for /chi glyph dots.")
    parser.add_argument("--output", default="data/chinese_strokes.txt")
    parser.add_argument("--chars", default="")
    parser.add_argument("--extra-text", action="append", default=[])
    parser.add_argument("--limit", type=int, default=3500)
    parser.add_argument("--sleep", type=float, default=0.03)
    args = parser.parse_args()

    frequency_text = fetch_frequency_text()
    text = DEFAULT_SEED + "\n" + args.chars + "\n" + frequency_text + "\n" + load_extra_text(args.extra_text)
    chars = list(iter_chars(text))[: max(1, args.limit)]
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "# Hanzi stroke medians for DDNet /chi.",
        "# Source: Hanzi Writer Data / Make Me A Hanzi.",
        "# Format: HEX_CODEPOINT;x,y x,y|x,y x,y",
    ]
    print("[info] downloading Make Me A Hanzi graphics.txt")
    graphics_index = fetch_graphics_index()
    print(f"[info] loaded {len(graphics_index)} source glyphs")

    ok = 0
    missing = []
    for index, char in enumerate(chars, start=1):
        try:
            medians = graphics_index.get(char)
            strokes = normalize_medians(medians or [])
            if strokes:
                lines.append(encode_line(char, strokes))
                ok += 1
            else:
                missing.append(char)
        except Exception as exc:
            missing.append(char)
            print(f"[warn] failed {char} U+{ord(char):04X}: {exc}")
        if index % 50 == 0:
            print(f"[info] {index}/{len(chars)} chars, {ok} ok")

    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[done] wrote {ok} glyphs to {out}")
    if missing:
        sample = "".join(missing[:40])
        print(f"[warn] missing {len(missing)} chars: {sample}")


if __name__ == "__main__":
    main()
