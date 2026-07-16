# -*- coding: utf-8 -*-

import os
import json
import re
import requests
import shutil
import subprocess
import threading
import hashlib
import uuid
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import quote
from selenium import webdriver
from flask import Flask, request, jsonify
import boto3
from pydub import AudioSegment
from dotenv import load_dotenv
from qqbot_bridge import QQBotBridge, QQBotConfig, format_playlist_message, read_playlist_snapshot
from music_stats import MusicStatsStore, format_guess_rank, format_music_rank, format_personal_rank
from undercover_game import UndercoverStore
from turtle_soup import TurtleSoupStore

# --- 1. 配置部分 ---

REQUEST_TIMEOUT = (10, 60)
PROJECT_ROOT = Path(__file__).resolve().parent
DEFAULT_SEARCH_LIMIT = 10
MAX_SEARCH_LIMIT = 50

load_dotenv(PROJECT_ROOT / ".env")


def _env(name, default=""):
    value = os.environ.get(name)
    return value if value not in (None, "") else default


def _env_int(name, default, minimum=None, maximum=None):
    try:
        value = int(_env(name, str(default)))
    except (TypeError, ValueError):
        value = default
    if minimum is not None:
        value = max(minimum, value)
    if maximum is not None:
        value = min(maximum, value)
    return value


@dataclass(frozen=True)
class AppConfig:
    netease_base_url: str
    download_dir: Path
    cover_dir: Path
    cookie_file: Path
    origin_maps_dir: Path
    prepared_maps_dir: Path
    map_patcher: str
    s3_endpoint_url: str
    s3_access_key: str
    s3_secret_key: str
    s3_bucket_name: str
    s3_public_domain: str
    s3_object_acl: str
    webmaps_base_path: Path
    host: str
    port: int
    music_stats_db: Path
    undercover_db: Path
    undercover_words_file: Path
    turtle_soup_db: Path
    turtle_soup_cases_file: Path
    map_patcher_timeout: int
    upload_workers: int

    @classmethod
    def from_env(cls):
        base_url = _env("BPMUSIC_NETEASE_BASE_URL", "http://music.163.com").rstrip("/")
        return cls(
            netease_base_url=base_url,
            download_dir=Path(_env("BPMUSIC_DOWNLOAD_DIR", "data/musicso")),
            cover_dir=Path(_env("BPMUSIC_COVER_DIR", "data/musicso/covers")),
            cookie_file=Path(_env("BPMUSIC_COOKIE_FILE", "netease_cookies.json")),
            origin_maps_dir=Path(_env("BPMUSIC_ORIGIN_MAPS_DIR", "data/originmaps")),
            prepared_maps_dir=Path(_env("BPMUSIC_PREPARED_MAPS_DIR", "data/musico/prepared_maps")),
            map_patcher=_env("BPMUSIC_MAP_PATCHER"),
            s3_endpoint_url=_env("BPMUSIC_S3_ENDPOINT_URL"),
            s3_access_key=_env("BPMUSIC_S3_ACCESS_KEY"),
            s3_secret_key=_env("BPMUSIC_S3_SECRET_KEY"),
            s3_bucket_name=_env("BPMUSIC_S3_BUCKET_NAME"),
            s3_public_domain=_env("BPMUSIC_S3_PUBLIC_DOMAIN"),
            s3_object_acl=_env("BPMUSIC_S3_OBJECT_ACL"),
            webmaps_base_path=Path(_env("BPMUSIC_WEBMAPS_BASE_PATH", "data/webmaps")),
            host=_env("BPMUSIC_HOST", "127.0.0.1"),
            port=int(_env("BPMUSIC_PORT", "5000")),
            music_stats_db=Path(_env("BPMUSIC_STATS_DB", "data/musico/music_history.sqlite3")),
            undercover_db=Path(_env("BPMUSIC_UNDERCOVER_DB", "data/musico/undercover.sqlite3")),
            undercover_words_file=Path(_env("BPMUSIC_UNDERCOVER_WORDS", "data/musico/undercover_words.txt")),
            turtle_soup_db=Path(_env("BPMUSIC_TURTLE_SOUP_DB", "data/musico/turtle_soup.sqlite3")),
            turtle_soup_cases_file=Path(_env("BPMUSIC_TURTLE_SOUP_CASES", "data/musico/turtle_soup_cases.json")),
            map_patcher_timeout=_env_int("BPMUSIC_MAP_PATCHER_TIMEOUT", 90, minimum=10),
            upload_workers=_env_int("BPMUSIC_UPLOAD_WORKERS", 2, minimum=1, maximum=4),
        )

    @property
    def search_api(self):
        return f"{self.netease_base_url}/api/search/get/web"

    @property
    def song_url_api(self):
        return f"{self.netease_base_url}/api/song/enhance/player/url"

    @property
    def song_detail_api(self):
        return f"{self.netease_base_url}/api/song/detail"

    @property
    def lyric_api(self):
        return f"{self.netease_base_url}/api/song/lyric"

    def s3_ready(self):
        return all([
            self.s3_endpoint_url,
            self.s3_access_key,
            self.s3_secret_key,
            self.s3_bucket_name,
            self.s3_public_domain,
        ])


CONFIG = AppConfig.from_env()
HTTP = requests.Session()
DOWNLOAD_LOCKS = {}
DOWNLOAD_LOCKS_GUARD = threading.Lock()
MAP_PATCH_LOCK = threading.Lock()
UPLOAD_EXECUTOR = ThreadPoolExecutor(max_workers=CONFIG.upload_workers, thread_name_prefix="bpmusic-upload")
QQBOT_BRIDGE = QQBotBridge(QQBotConfig.from_env(PROJECT_ROOT))
MUSIC_STATS = MusicStatsStore(CONFIG.music_stats_db)
UNDERCOVER = UndercoverStore(CONFIG.undercover_db, CONFIG.undercover_words_file)
TURTLE_SOUP = TurtleSoupStore(CONFIG.turtle_soup_db, CONFIG.turtle_soup_cases_file)
SERVER_STATE_LOCK = threading.Lock()
DDNET_PROFILE_LOCK = threading.Lock()
DDNET_PROFILE_CACHE = {}
GUESS_LOCK = threading.Lock()
GUESS_ROUND = {
    "round_id": "",
    "song_id": "",
    "title": "",
    "artist": "",
    "winner": "",
}
SERVER_STATE = {
    "map_name": "",
    "players": [],
    "updated_at": 0,
}


def server_state_snapshot():
    with SERVER_STATE_LOCK:
        return {
            "map_name": SERVER_STATE["map_name"],
            "players": list(SERVER_STATE["players"]),
            "updated_at": SERVER_STATE["updated_at"],
        }


def format_server_status():
    state = server_state_snapshot()
    if not state["updated_at"] or time.time() - state["updated_at"] > 15:
        return "听歌服状态暂不可用，服务端可能未启动。"
    try:
        queue = read_playlist_snapshot(QQBOT_BRIDGE.config.playlist_file)
        songs = queue["songs"]
        current_index = queue["current_index"]
        if queue["is_playing"] and 0 <= current_index < len(songs):
            song = songs[current_index]
            elapsed = max(0, int(time.time()) - queue["start_time"]) if queue["start_time"] > 0 else 0
            duration = int(song["duration"] or queue["duration"])
            elapsed = min(elapsed, duration) if duration > 0 else elapsed
            music = f"{song['title']} - {song['artist']} {_format_seconds(elapsed)}/{_format_seconds(duration)}"
        else:
            music = "当前未播放"
        queue_size = len(songs)
    except RuntimeError:
        music = "队列状态暂不可用"
        queue_size = 0
    return (
        f"听歌服状态\n在线：{len(state['players'])} 人\n地图：{state['map_name'] or '未知'}\n"
        f"音乐：{music}\n队列：{queue_size} 首"
    )


def _format_seconds(seconds):
    seconds = max(0, int(seconds))
    return f"{seconds // 60:02d}:{seconds % 60:02d}"


def format_server_players():
    state = server_state_snapshot()
    if not state["updated_at"] or time.time() - state["updated_at"] > 15:
        return "玩家列表暂不可用，服务端可能未启动。"
    if not state["players"]:
        return "听歌服当前无人在线。"
    return "在线玩家（不显示 IP）\n" + "\n".join(
        f"{index}. {name}" for index, name in enumerate(state["players"], start=1)
    )


def _normalize_guess_text(value):
    return re.sub(r"[\W_]+", "", str(value or "").casefold(), flags=re.UNICODE)


def _mask_guess_text(value):
    text = str(value or "")
    chars = [char for char in text if not char.isspace()]
    if len(chars) <= 1:
        return "□" * len(chars)
    reveal_first = True
    reveal_last = len(chars) >= 4
    seen = 0
    output = []
    for char in text:
        if char.isspace():
            output.append(" ")
            continue
        reveal = (reveal_first and seen == 0) or (reveal_last and seen == len(chars) - 1)
        output.append(char if reveal else "□")
        seen += 1
    return "".join(output)


def current_guess_song():
    queue = read_playlist_snapshot(QQBOT_BRIDGE.config.playlist_file)
    songs = queue["songs"]
    current_index = queue["current_index"]
    if not queue["is_playing"] or current_index < 0 or current_index >= len(songs):
        return None, "当前没有正在播放的歌曲，猜歌局开不起来。"
    song = songs[current_index]
    round_id = f"{song['song_id']}|{queue['start_time']}"
    return {
        "round_id": round_id,
        "song_id": song["song_id"],
        "title": song["title"],
        "artist": song["artist"],
    }, None


def guess_game(source, requester_id, requester_name, answer=""):
    try:
        song, error = current_guess_song()
    except RuntimeError:
        return "播放队列状态暂不可用，暂时不能猜歌。"
    if error:
        return error

    answer = QQBotBridge.sanitize_relay_text(answer, 80)
    with GUESS_LOCK:
        if GUESS_ROUND["round_id"] != song["round_id"]:
            GUESS_ROUND.update({**song, "winner": ""})

        if not answer:
            return (
                "猜当前歌\n"
                f"歌名：{_mask_guess_text(song['title'])}\n"
                f"歌手：{_mask_guess_text(song['artist'])}\n"
                "发送 /guess 歌名 抢答"
            )

        if GUESS_ROUND["winner"]:
            return f"这首已经被 {GUESS_ROUND['winner']} 猜中了，答案是：{song['title']} - {song['artist']}"

        normalized_answer = _normalize_guess_text(answer)
        title_answer = _normalize_guess_text(song["title"])
        full_answer = _normalize_guess_text(f"{song['title']} {song['artist']}")
        if normalized_answer not in {title_answer, full_answer}:
            return "没猜中，再听听。"

        identity = identity_context(source, requester_id, requester_name)
        winner = identity["display_name"]
        GUESS_ROUND["winner"] = winner
        MUSIC_STATS.record_guess_win(source, requester_id, winner, song["song_id"], song["title"], song["artist"])
        return f"{winner} 猜中了！答案：{song['title']} - {song['artist']}，猜歌榜 +1"


def guess_rank_text():
    return format_guess_rank(MUSIC_STATS.guess_rank())


def bind_qq_identity(qq_id, qq_name, code):
    result = MUSIC_STATS.consume_bind_code(code, qq_id, qq_name)
    return result["message"]


def unbind_qq_identity(qq_id):
    result = MUSIC_STATS.unbind_qq(qq_id)
    return result["message"]


def format_identity_profile(qq_id):
    binding = MUSIC_STATS.binding_for_qq(qq_id)
    if not binding:
        return "尚未绑定游戏角色。请在游戏内输入 /bindqq，再发送 /bind 验证码。"
    state = server_state_snapshot()
    online_name = next(
        (
            name for name in state["players"]
            if name.casefold() == binding["game_name"].casefold()
        ),
        None,
    )
    personal = MUSIC_STATS.personal("qq", qq_id)
    online_text = "在线" if online_name else "离线"
    map_text = state["map_name"] if online_name and state["map_name"] else "-"
    return (
        f"听歌服角色：{binding['game_name']}\n"
        f"QQ 昵称：{binding['qq_name']}\n"
        f"身份：{binding['role']}\n"
        f"状态：{online_text}\n"
        f"当前地图：{map_text}\n"
        f"累计点歌：{personal['total']} 次"
    )


def fetch_ddnet_profile(player_name):
    cache_key = player_name.casefold()
    now = time.time()
    with DDNET_PROFILE_LOCK:
        cached = DDNET_PROFILE_CACHE.get(cache_key)
        if cached and now - cached["time"] < 300:
            return cached["data"]
    response = HTTP.get(
        "https://ddnet.org/players/",
        params={"json2": player_name},
        headers={"User-Agent": "BauPlayerMusic/1.0"},
        timeout=(10, 30),
    )
    response.raise_for_status()
    data = response.json()
    if not isinstance(data, dict) or not data.get("player"):
        raise RuntimeError("DDNet 未找到该玩家")
    with DDNET_PROFILE_LOCK:
        DDNET_PROFILE_CACHE[cache_key] = {"time": now, "data": data}
    return data


def _rank_text(value):
    rank = (value or {}).get("rank")
    return f"第 {rank} 名" if rank else "未上榜"


def format_ddnet_profile(data):
    points = data.get("points") or {}
    last_year = data.get("points_last_year") or {}
    last_month = data.get("points_last_month") or {}
    last_week = data.get("points_last_week") or {}
    return (
        f"DDNet 官方资料：{data['player']}\n"
        f"总分：{points.get('points', 0)}/{points.get('total', 0)}，{_rank_text(points)}\n"
        f"近365天：{last_year.get('points', 0)} 分，{_rank_text(last_year)}\n"
        f"近30天：{last_month.get('points', 0)} 分，{_rank_text(last_month)}\n"
        f"近7天：{last_week.get('points', 0)} 分，{_rank_text(last_week)}\n"
        f"近365天游戏时长：{data.get('hours_played_past_365_days', 0)} 小时"
    )


def render_ddnet_profile(player_name):
    output_dir = PROJECT_ROOT / "data" / "musico" / "ddnet_profiles"
    output_dir.mkdir(parents=True, exist_ok=True)
    file_key = hashlib.sha256(player_name.casefold().encode("utf-8")).hexdigest()[:20]
    output_path = output_dir / f"{file_key}.png"
    if output_path.exists() and time.time() - output_path.stat().st_mtime < 600:
        return output_path

    options = webdriver.EdgeOptions()
    options.add_argument("--headless=new")
    options.add_argument("--disable-gpu")
    options.add_argument("--window-size=1500,1000")
    options.add_argument("--force-device-scale-factor=1")
    driver = webdriver.Edge(options=options)
    try:
        from selenium.webdriver.common.by import By
        from selenium.webdriver.support import expected_conditions as EC
        from selenium.webdriver.support.ui import WebDriverWait

        driver.get(f"https://ddnet.org/players/{quote(player_name, safe='')}/")
        element = WebDriverWait(driver, 20).until(
            EC.visibility_of_element_located((By.CSS_SELECTOR, "#global"))
        )
        if not element.screenshot(str(output_path)):
            raise RuntimeError("DDNet 玩家资料截图失败")
    finally:
        driver.quit()
    return output_path


def _ddnet_player_from_query(qq_id, player_name):
    player_name = QQBotBridge.sanitize_relay_text(player_name, 32)
    if player_name:
        return player_name, None
    binding = MUSIC_STATS.binding_for_qq(qq_id)
    if binding:
        return binding["game_name"], None
    return "", "请先绑定角色，或直接使用：/ddnet 玩家名"


def ddnet_profile_for_qq(qq_id, player_name=""):
    player_name, error = _ddnet_player_from_query(qq_id, player_name)
    if error:
        return error
    if not player_name:
        return "请先在游戏内输入 /bindqq，并在群里使用 /bind 验证码完成绑定。"
    data = fetch_ddnet_profile(player_name)
    image_path = render_ddnet_profile(player_name)
    return {"text": format_ddnet_profile(data), "image_path": image_path}


def personal_rank_for_identity(source, requester_id, name):
    binding = (
        MUSIC_STATS.binding_for_qq(requester_id)
        if source == "qq"
        else MUSIC_STATS.binding_for_game(requester_id)
        if source == "game"
        else None
    )
    display_name = binding["game_name"] if binding else name
    return format_personal_rank(
        display_name, MUSIC_STATS.personal(source, requester_id)
    )


def identity_context(source, identity_id, display_name):
    binding = (
        MUSIC_STATS.binding_for_qq(identity_id)
        if source == "qq"
        else MUSIC_STATS.binding_for_game(identity_id)
        if source == "game"
        else None
    )
    if not binding:
        return {
            "key": f"{source}:{str(identity_id).casefold()}",
            "display_name": display_name,
            "role": "user",
        }
    return {
        "key": f"bound:{binding['qq_id']}",
        "display_name": binding["game_name"],
        "role": binding["role"],
    }


QQBOT_BRIDGE.set_command_providers(
    format_server_status,
    format_server_players,
    lambda: format_music_rank(MUSIC_STATS.summary()),
    personal_rank_for_identity,
    bind_qq_identity,
    format_identity_profile,
    ddnet_profile_for_qq,
    identity_context,
    unbind_qq_identity,
    guess_game,
    guess_rank_text,
)


def api_error(message, status=400, **extra):
    payload = {"status": "error", "message": message}
    payload.update(extra)
    return jsonify(payload), status


def qqbot_server_authorized():
    expected = QQBOT_BRIDGE.config.server_token
    if not expected:
        return True
    return request.headers.get("Authorization") == f"Bearer {expected}"


def int_arg(name, default, minimum=None, maximum=None):
    raw_value = request.args.get(name)
    try:
        value = int(raw_value) if raw_value not in (None, "") else default
    except ValueError:
        value = default
    if minimum is not None:
        value = max(minimum, value)
    if maximum is not None:
        value = min(maximum, value)
    return value


def ffmpeg_binary():
    return shutil.which("ffmpeg")


def configure_audio_tools():
    binary = ffmpeg_binary()
    if binary:
        AudioSegment.converter = binary
        AudioSegment.ffmpeg = binary
    return binary


def ffmpeg_available():
    return ffmpeg_binary() is not None


def start_qqbot_bridge():
    QQBOT_BRIDGE.start()
    return QQBOT_BRIDGE


def read_opus_duration(opus_path):
    configure_audio_tools()
    try:
        audio = AudioSegment.from_file(str(opus_path), format="ogg")
        return len(audio) / 1000.0, None
    except Exception as e:
        return 0, str(e)


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def map_patcher_binary():
    if CONFIG.map_patcher:
        path = Path(CONFIG.map_patcher)
        return str(path if path.is_absolute() else path.resolve())

    executable = "music_map_patcher.exe" if os.name == "nt" else "music_map_patcher"
    bundled = PROJECT_ROOT / executable
    if bundled.exists():
        return str(bundled)

    found = shutil.which(executable)
    return found


def upload_webmap(file_name, local_file_path):
    if not CONFIG.s3_ready():
        return False, None, "对象存储配置不完整，请检查 BPMUSIC_S3_* 环境变量"

    object_key = file_name
    try:
        s3 = boto3.client(
            's3',
            endpoint_url=CONFIG.s3_endpoint_url,
            aws_access_key_id=CONFIG.s3_access_key,
            aws_secret_access_key=CONFIG.s3_secret_key
        )

        extra_args = {}
        if CONFIG.s3_object_acl:
            extra_args["ACL"] = CONFIG.s3_object_acl

        if extra_args:
            s3.upload_file(str(local_file_path), CONFIG.s3_bucket_name, object_key, ExtraArgs=extra_args)
        else:
            s3.upload_file(str(local_file_path), CONFIG.s3_bucket_name, object_key)

        final_url = f"https://{CONFIG.s3_public_domain}/{object_key}"
        return True, final_url, None
    except Exception as e:
        return False, None, f"上传到对象存储时发生错误: {e}"


UPLOAD_WEBMAP = upload_webmap


def upload_webmap_async(file_name, local_file_path):
    def worker():
        upload_success, upload_url, upload_error = UPLOAD_WEBMAP(file_name, local_file_path)
        if upload_success:
            print(f"[上传] 音乐地图已上传: {upload_url}")
        else:
            print(f"[上传] 音乐地图后台上传失败，不影响本地播放: {upload_error}")

    UPLOAD_EXECUTOR.submit(worker)


def install_generated_map(generated_path, final_path):
    generated_path = Path(generated_path)
    final_path = Path(final_path)
    if generated_path == final_path:
        return True, None
    try:
        os.replace(generated_path, final_path)
        return True, None
    except PermissionError as e:
        try:
            shutil.copyfile(generated_path, final_path)
            generated_path.unlink(missing_ok=True)
            print(f"[地图] 原子替换被 Windows 拒绝，已回退为覆盖写入: {final_path}")
            return True, None
        except Exception as copy_error:
            return False, f"安装音乐地图失败: {e}; 覆盖写入也失败: {copy_error}"
    except Exception as e:
        return False, f"安装音乐地图失败: {e}"


def prepare_music_map(song_id, map_name, target_map_path=None):
    safe_map_name = os.path.basename(map_name or "")
    if not safe_map_name:
        return False, {"message": "缺少地图名，无法生成音乐地图"}

    final_prepared_map = None
    patch_output_map = None
    if target_map_path:
        target_path = Path(target_map_path)
        if not target_path.is_absolute():
            return False, {"message": "target_map_path 必须是绝对路径"}
        expected_file_name = f"{safe_map_name}.map"
        if target_path.name.casefold() != expected_file_name.casefold():
            return False, {"message": f"target_map_path 文件名必须为 {expected_file_name}"}
        target_path.parent.mkdir(parents=True, exist_ok=True)
        final_prepared_map = target_path.resolve()
        patch_output_map = final_prepared_map.with_name(f".{final_prepared_map.name}.{uuid.uuid4().hex}.tmp")

    patcher = map_patcher_binary()
    if not patcher or not Path(patcher).exists():
        return False, {"message": "未找到 music_map_patcher，请重新构建 game-server 或配置 BPMUSIC_MAP_PATCHER"}

    origin_map = (CONFIG.origin_maps_dir / f"{safe_map_name}.map").resolve()
    audio_path = (CONFIG.download_dir / f"{song_id}.opus").resolve()
    if not origin_map.exists():
        return False, {"message": f"原始地图不存在: {origin_map}"}
    if not audio_path.exists():
        return False, {"message": f"音频文件不存在: {audio_path}"}

    if final_prepared_map is None:
        CONFIG.prepared_maps_dir.mkdir(parents=True, exist_ok=True)
        final_prepared_map = (CONFIG.prepared_maps_dir / f"{safe_map_name}_{song_id}_{uuid.uuid4().hex}.map").resolve()
        patch_output_map = final_prepared_map

    if patch_output_map is None:
        patch_output_map = final_prepared_map

    command = [patcher, str(origin_map), str(patch_output_map), str(song_id), str(audio_path)]
    with MAP_PATCH_LOCK:
        try:
            if patch_output_map != final_prepared_map and patch_output_map.exists():
                patch_output_map.unlink()
            patcher_path = Path(patcher)
            env = os.environ.copy()
            env["PATH"] = os.pathsep.join([
                str(patcher_path.parent),
                str(patcher_path.parent.parent),
                env.get("PATH", ""),
            ])
            result = subprocess.run(
                command,
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=env,
                timeout=CONFIG.map_patcher_timeout,
            )
            if result.stdout:
                print(result.stdout.strip())
            if result.stderr:
                print(result.stderr.strip())
        except subprocess.TimeoutExpired as e:
            if patch_output_map != final_prepared_map and patch_output_map.exists():
                patch_output_map.unlink(missing_ok=True)
            output = "\n".join(part for part in [e.stdout, e.stderr] if part)
            return False, {"message": f"音乐地图生成超时，超过 {CONFIG.map_patcher_timeout} 秒", "tool_output": output}
        except subprocess.CalledProcessError as e:
            if patch_output_map != final_prepared_map and patch_output_map.exists():
                patch_output_map.unlink(missing_ok=True)
            output = "\n".join(part for part in [e.stdout, e.stderr] if part)
            return False, {"message": f"音乐地图生成失败，退出码: {e.returncode}", "tool_output": output}
        except Exception as e:
            if patch_output_map != final_prepared_map and patch_output_map.exists():
                patch_output_map.unlink(missing_ok=True)
            return False, {"message": f"音乐地图生成失败: {e}"}

        if not patch_output_map.exists():
            return False, {"message": f"音乐地图生成后未找到候选文件: {patch_output_map}"}
        if patch_output_map != final_prepared_map:
            install_success, install_error = install_generated_map(patch_output_map, final_prepared_map)
            if not install_success:
                patch_output_map.unlink(missing_ok=True)
                return False, {"message": install_error}

        map_sha256 = sha256_file(final_prepared_map)
        CONFIG.webmaps_base_path.mkdir(parents=True, exist_ok=True)
        webmap_file_name = f"{safe_map_name}_{map_sha256}.map"
        webmap_path = CONFIG.webmaps_base_path / webmap_file_name
        shutil.copy2(final_prepared_map, webmap_path)

    upload_queued = False
    if CONFIG.s3_ready():
        upload_webmap_async(webmap_file_name, webmap_path)
        upload_queued = True

    return True, {
        "prepared_map_path": str(final_prepared_map),
        "map_sha256": map_sha256,
        "webmap_path": str(webmap_path.resolve()),
        "upload_queued": upload_queued,
    }


def song_download_lock(song_id):
    with DOWNLOAD_LOCKS_GUARD:
        lock = DOWNLOAD_LOCKS.get(song_id)
        if lock is None:
            lock = threading.Lock()
            DOWNLOAD_LOCKS[song_id] = lock
        return lock

# --- 2. 核心功能函数 ---

def search_songs(keyword, limit=10):
    params = {'s': keyword, 'type': 1, 'limit': limit, 'offset': 0}
    try:
        response = HTTP.post(CONFIG.search_api, data=params, timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"[错误] 搜索歌曲 '{keyword}' 时出错: {e}")
        return None

def get_song_url(song_id, cookies):
    params = {'ids': f"[{song_id}]", 'br': 320000}
    try:
        response = HTTP.get(CONFIG.song_url_api, params=params, cookies=cookies, timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"[错误] 获取歌曲 ID '{song_id}' 的链接时出错: {e}")
        return None


def get_song_cover_url(song_id):
    """Return the album-art URL exposed by the same NetEase API used for songs."""
    try:
        response = HTTP.get(
            CONFIG.song_detail_api,
            params={'ids': f'[{song_id}]'},
            timeout=REQUEST_TIMEOUT,
        )
        response.raise_for_status()
        data = response.json()
        songs = data.get('songs') or []
        if not songs:
            return None
        album = songs[0].get('album') or {}
        pic_url = album.get('picUrl')
        return pic_url if isinstance(pic_url, str) and pic_url.startswith(('http://', 'https://')) else None
    except (requests.exceptions.RequestException, ValueError, KeyError, IndexError) as e:
        print(f"[封面] 获取歌曲 ID '{song_id}' 的专辑信息失败: {e}")
        return None


def download_song_cover(song_id):
    """Fetch album art as a predictable local JPEG path.

    This intentionally does not make a failed cover download fatal to music playback.
    """
    cover_path = CONFIG.cover_dir / f"{song_id}.jpg"
    if cover_path.exists() and cover_path.stat().st_size > 0:
        return True, {"cover_path": str(cover_path.resolve()), "cover_cached": True}

    pic_url = get_song_cover_url(song_id)
    if not pic_url:
        return False, {"cover_message": "网易云未返回专辑封面地址"}

    tmp_path = CONFIG.cover_dir / f".{song_id}.{uuid.uuid4().hex}.jpg.tmp"
    try:
        CONFIG.cover_dir.mkdir(parents=True, exist_ok=True)
        response = HTTP.get(pic_url, stream=True, timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        content_type = response.headers.get("Content-Type", "").split(";", 1)[0].lower()
        if not content_type.startswith("image/"):
            return False, {"cover_message": f"封面地址返回的不是图片 ({content_type or '未知类型'})"}

        written = 0
        with tmp_path.open('wb') as f:
            for chunk in response.iter_content(chunk_size=64 * 1024):
                if not chunk:
                    continue
                written += len(chunk)
                if written > 20 * 1024 * 1024:
                    raise ValueError("封面文件超过 20 MiB 限制")
                f.write(chunk)
        if written == 0:
            raise ValueError("封面文件为空")
        os.replace(tmp_path, cover_path)
        print(f"[封面] 已保存 {cover_path}")
        return True, {
            "cover_path": str(cover_path.resolve()),
            "cover_url": pic_url,
            "cover_cached": False,
        }
    except (requests.exceptions.RequestException, OSError, ValueError) as e:
        print(f"[封面] 下载歌曲 ID '{song_id}' 的封面失败: {e}")
        tmp_path.unlink(missing_ok=True)
        return False, {"cover_message": str(e)}


def describe_song_url_failure(song_url_data):
    if not song_url_data:
        return "网易云歌曲链接接口请求失败", {}

    entries = song_url_data.get("data") or []
    if not entries:
        return "网易云歌曲链接接口未返回歌曲数据", {
            "provider_code": song_url_data.get("code"),
        }

    entry = entries[0]
    details = {
        "provider_code": entry.get("code"),
        "fee": entry.get("fee"),
        "payed": entry.get("payed"),
    }
    if entry.get("fee") == 1 and not entry.get("payed"):
        return "该歌曲需要网易云会员权限，当前 Cookie 对应账号无播放权限或登录态已失效", details
    if entry.get("code") == -110:
        return "网易云拒绝提供该歌曲音频，通常是版权、地区或账号权限限制", details
    return "网易云未返回有效音频链接", details


def _download_and_save_lyrics(song_id, title):
    lrc_path = CONFIG.download_dir / f"{song_id}.lrc"
    if lrc_path.exists():
        print(f"[跳过] 歌词文件 {lrc_path.name} 已存在。")
        return
    params = {'id': song_id, 'lv': -1, 'tv': -1}
    try:
        response = HTTP.get(CONFIG.lyric_api, params=params, timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        data = response.json()
        if data.get('lrc') and data['lrc'].get('lyric'):
            lyrics = data['lrc']['lyric']
            with lrc_path.open("w", encoding="utf-8") as f:
                f.write(lyrics)
            print(f"[歌词] 已成功保存歌词到 {lrc_path.name}")
        else:
            print(f"[歌词] 歌曲 '{title}' 没有找到歌词。")
    except Exception as e:
        print(f"[错误] 下载歌曲 '{title}' 的歌词时失败: {e}")

def save_cookies(cookies):
    with CONFIG.cookie_file.open('w') as f:
        json.dump(cookies, f)

def load_cookies():
    if CONFIG.cookie_file.exists():
        with CONFIG.cookie_file.open('r') as f:
            return json.load(f)
    return None

def login_and_get_cookies():
    print("[登录] 浏览器窗口将会打开，请您登录网易云音乐。")
    print("[登录] 请在网页上完成登录后，回到此命令行窗口并按 Enter 键继续。")
    driver = None
    try:
        driver = webdriver.Chrome()
        driver.get("https://music.163.com/#/login")
        input("[登录] 在您成功登录后，请按 Enter 键...")
        cookies = {cookie['name']: cookie['value'] for cookie in driver.get_cookies()}
        save_cookies(cookies)
        print("[登录] Cookies 已成功保存！")
        return cookies
    except Exception as e:
        print(f"[错误] 登录过程中发生错误: {e}")
        return None
    finally:
        if driver:
            driver.quit()


def _perform_download_and_conversion(song_id, title, artist, cookies):
    with song_download_lock(song_id):
        return _perform_download_and_conversion_unlocked(song_id, title, artist, cookies)


def _perform_download_and_conversion_unlocked(song_id, title, artist, cookies):
    """
    同步执行下载和转换，并返回包含时长信息的结果。
    """
    log_display_name = f"{title} - {artist}"
    print(f"[处理开始] 开始处理: {log_display_name} (ID: {song_id})")
    opus_path = CONFIG.download_dir / f"{song_id}.opus"
    cover_success, cover_data = download_song_cover(song_id)
    if not cover_success:
        print(f"[封面] 跳过 '{log_display_name}' 的封面: {cover_data.get('cover_message', '未知错误')}")

    if opus_path.exists():
        _download_and_save_lyrics(song_id, title)
        duration_seconds, duration_error = read_opus_duration(opus_path)
        if duration_seconds <= 0:
            error_msg = f"[错误] 无法获取已存在文件 '{opus_path.name}' 的时长: {duration_error}"
            print(error_msg)
            return False, {"message": error_msg}

        success_msg = f"歌曲 '{log_display_name}' ({opus_path.name}) 已存在，跳过下载。"
        print(f"[时长] 已存在文件 '{opus_path.name}' 的时长为: {duration_seconds:.2f} 秒")
        return True, {"message": success_msg, "duration": duration_seconds, **cover_data}

    configure_audio_tools()
    if not ffmpeg_available():
        error_msg = "[错误] 未找到 ffmpeg，请安装 FFmpeg 并确保 ffmpeg 在 PATH 中。"
        print(error_msg)
        return False, {"message": error_msg}

    _download_and_save_lyrics(song_id, title)

    song_url_data = get_song_url(song_id, cookies)
    if not (song_url_data and song_url_data.get('data') and song_url_data['data'][0].get('url')):
        failure_reason, failure_details = describe_song_url_failure(song_url_data)
        error_msg = f"[错误] 无法获取 '{log_display_name}' 的有效下载链接: {failure_reason}。"
        print(error_msg)
        return False, {"message": error_msg, **failure_details}

    download_url = song_url_data['data'][0]['url']
    mp3_path = CONFIG.download_dir / f"{song_id}.mp3"
    mp3_tmp_path = CONFIG.download_dir / f".{song_id}.{uuid.uuid4().hex}.mp3.tmp"

    try:
        response = HTTP.get(download_url, stream=True, timeout=REQUEST_TIMEOUT)
        response.raise_for_status()
        with mp3_tmp_path.open('wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)
        os.replace(mp3_tmp_path, mp3_path)
        print(f"[下载完成] {mp3_path.name}")
    except requests.exceptions.RequestException as e:
        error_msg = f"[错误] 下载 '{log_display_name}' 时失败: {e}"
        print(error_msg)
        mp3_tmp_path.unlink(missing_ok=True)
        return False, {"message": error_msg}
    except Exception as e:
        error_msg = f"[错误] 保存 '{log_display_name}' 时失败: {e}"
        print(error_msg)
        mp3_tmp_path.unlink(missing_ok=True)
        return False, {"message": error_msg}

    opus_tmp_path = CONFIG.download_dir / f".{song_id}.{uuid.uuid4().hex}.tmp.opus"
    command = [
        'ffmpeg', '-i', str(mp3_path), '-c:a', 'libopus', '-vbr', 'on',
        '-b:a', '128k', '-f', 'opus', '-y', '-loglevel', 'error', str(opus_tmp_path)
    ]
    try:
        subprocess.run(command, check=True)
        os.replace(opus_tmp_path, opus_path)
        print(f"[转换成功] {opus_path.name}")
    except FileNotFoundError:
        error_msg = "[错误] 未找到 ffmpeg，请安装 FFmpeg 并确保 ffmpeg 在 PATH 中。"
        print(error_msg)
        opus_tmp_path.unlink(missing_ok=True)
        if mp3_path.exists():
            mp3_path.unlink()
        return False, {"message": error_msg}
    except subprocess.CalledProcessError as e:
        error_msg = f"[错误] FFmpeg 转换失败，退出码: {e.returncode}"
        print(error_msg)
        opus_tmp_path.unlink(missing_ok=True)
        if mp3_path.exists():
            mp3_path.unlink()
        return False, {"message": error_msg}
    except Exception as e:
        error_msg = f"[错误] FFmpeg 转换时失败: {e}"
        print(error_msg)
        opus_tmp_path.unlink(missing_ok=True)
        if mp3_path.exists():
            mp3_path.unlink()
        return False, {"message": error_msg}
    
    duration_seconds, duration_error = read_opus_duration(opus_path)
    if duration_seconds <= 0:
        error_msg = f"[错误] 无法使用 pydub 获取音频时长: {duration_error}"
        print(error_msg)
        if opus_path.exists():
            opus_path.unlink()
        if mp3_path.exists():
            mp3_path.unlink()
        return False, {"message": error_msg}

    print(f"[时长] 获取到 '{os.path.basename(opus_path)}' 的时长为: {duration_seconds:.2f} 秒")
        
    if mp3_path.exists():
        mp3_path.unlink()
    
    final_filename = opus_path.name
    success_msg = f"歌曲 '{log_display_name}' 已成功保存为 '{final_filename}'。"
    return True, {"message": success_msg, "duration": duration_seconds, **cover_data}

# --- 3. Flask API 服务 ---

app = Flask(__name__)
NETEASE_COOKIES = {}


@app.route('/health', methods=['GET'])
def api_health():
    ffmpeg_path = ffmpeg_binary()
    return jsonify({
        "status": "ok",
        "ffmpeg": ffmpeg_path is not None,
        "ffmpeg_path": ffmpeg_path,
        "s3_ready": CONFIG.s3_ready(),
        "download_dir": str(CONFIG.download_dir.resolve()),
        "origin_maps_dir": str(CONFIG.origin_maps_dir.resolve()),
        "prepared_maps_dir": str(CONFIG.prepared_maps_dir.resolve()),
        "webmaps_base_path": str(CONFIG.webmaps_base_path.resolve()),
        "map_patcher": map_patcher_binary(),
        "qqbot": QQBOT_BRIDGE.status(),
    })


@app.route('/search', methods=['GET'])
def api_search_compatible():
    query = request.args.get('name')
    if not query:
        return api_error("缺少歌曲名称参数 'name'")
    limit = int_arg("limit", DEFAULT_SEARCH_LIMIT, 1, MAX_SEARCH_LIMIT)
    print(f"[API /search] 收到搜索请求: '{query}' (limit={limit})")
    search_results = search_songs(query, limit=limit)
    if not search_results or not search_results.get('result') or not search_results['result'].get('songs'):
        return jsonify([])
    songs = search_results['result']['songs']
    response_list = []
    for song in songs:
        response_list.append({
            "title": song['name'],
            "artist": ", ".join([artist['name'] for artist in song['artists']]),
            "page_url": str(song['id'])
        })
    return jsonify(response_list)


@app.route('/queue', methods=['GET'])
def api_queue_snapshot():
    try:
        snapshot = read_playlist_snapshot(QQBOT_BRIDGE.config.playlist_file)
    except RuntimeError as error:
        return api_error(str(error), 503)

    return jsonify({
        "status": "success",
        "playlist_file": str(QQBOT_BRIDGE.config.playlist_file.resolve()),
        "current_index": snapshot["current_index"],
        "start_time": snapshot["start_time"],
        "duration": snapshot["duration"],
        "is_playing": snapshot["is_playing"],
        "songs": snapshot["songs"],
        "text": format_playlist_message(snapshot, QQBOT_BRIDGE.config.max_songs),
    })


@app.route('/qqbot/send', methods=['POST'])
def api_qqbot_send():
    if not qqbot_server_authorized():
        return api_error("QQ Bot 服务端鉴权失败", 401)
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")

    data = request.get_json(silent=True) or {}
    success, message = QQBOT_BRIDGE.send_server_message(
        data.get("player_name"),
        data.get("message"),
    )
    if not success:
        return api_error(message, 503)
    return jsonify({"status": "success", "message": message})


@app.route('/qqbot/poll', methods=['POST'])
def api_qqbot_poll():
    if not qqbot_server_authorized():
        return api_error("QQ Bot 服务端鉴权失败", 401)
    return jsonify({
        "status": "success",
        "messages": QQBOT_BRIDGE.poll_server_messages(10),
    })


@app.route('/history/record', methods=['POST'])
def api_history_record():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")

    data = request.get_json(silent=True) or {}
    required = ("song_id", "title", "artist", "requester_name")
    if not all(data.get(key) for key in required):
        return api_error("请求体缺少点歌记录字段")
    record_id = MUSIC_STATS.record(
        data["song_id"],
        data["title"],
        data["artist"],
        data.get("requester_source", "game"),
        data.get("requester_id") or data["requester_name"],
        data["requester_name"],
    )
    return jsonify({"status": "success", "record_id": record_id})


@app.route('/identity/code', methods=['POST'])
def api_identity_code():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")
    data = request.get_json(silent=True) or {}
    game_name = QQBotBridge.sanitize_relay_text(data.get("game_name"), 32)
    if not game_name:
        return api_error("缺少游戏昵称")
    result = MUSIC_STATS.create_bind_code(game_name, ttl_seconds=300)
    return jsonify({
        "status": "success",
        **result,
        "text": (
            f"绑定验证码：{result['code']}\n"
            "请在 5 分钟内到指定 QQ 群发送：@机器人 /bind 验证码"
        ),
    })


@app.route('/identity/status', methods=['GET'])
def api_identity_status():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    game_name = QQBotBridge.sanitize_relay_text(request.args.get("game_name"), 32)
    if not game_name:
        return api_error("缺少游戏昵称")
    binding = MUSIC_STATS.binding_for_game(game_name)
    if not binding:
        return jsonify({
            "status": "success",
            "bound": False,
            "text": f"{game_name} 尚未绑定 QQ。输入 /bindqq 可生成验证码。",
        })
    qq_id = str(binding["qq_id"])
    masked_qq = qq_id[:3] + "***" + qq_id[-2:] if len(qq_id) > 5 else "***"
    return jsonify({
        "status": "success",
        "bound": True,
        "game_name": binding["game_name"],
        "role": binding["role"],
        "text": f"{binding['game_name']} 已绑定 QQ {masked_qq}，身份：{binding['role']}",
    })


@app.route('/server/state', methods=['POST'])
def api_server_state():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")
    data = request.get_json(silent=True) or {}
    players = data.get("players") or []
    if not isinstance(players, list):
        return api_error("players 必须是数组")
    clean_players = []
    for name in players[:128]:
        clean_name = QQBotBridge.sanitize_relay_text(name, 32)
        if clean_name:
            clean_players.append(clean_name)
    with SERVER_STATE_LOCK:
        SERVER_STATE["map_name"] = QQBotBridge.sanitize_relay_text(data.get("map_name"), 64)
        SERVER_STATE["players"] = clean_players
        SERVER_STATE["updated_at"] = int(time.time())
    return jsonify({"status": "success"})


@app.route('/stats/musicrank', methods=['GET'])
def api_music_rank():
    summary = MUSIC_STATS.summary(limit=int_arg("limit", 5, 1, 10))
    return jsonify({"status": "success", **summary, "text": format_music_rank(summary)})


@app.route('/stats/myrank', methods=['GET'])
def api_my_rank():
    source = request.args.get("source", "game")
    requester_id = request.args.get("id", "")
    name = request.args.get("name", requester_id or "用户")
    if not requester_id:
        return api_error("缺少用户 ID")
    personal = MUSIC_STATS.personal(source, requester_id, limit=int_arg("limit", 5, 1, 10))
    binding = personal.get("binding")
    if binding:
        name = binding["game_name"]
    return jsonify({
        "status": "success",
        "requester_source": source,
        "requester_id": requester_id,
        "name": name,
        **personal,
        "text": format_personal_rank(name, personal),
    })


@app.route('/guess', methods=['GET'])
def api_guess():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    source = request.args.get("source", "game")
    requester_id = request.args.get("id", "")
    name = request.args.get("name", requester_id or "玩家")
    answer = request.args.get("answer", "")
    if not requester_id:
        return api_error("缺少用户 ID")
    text = guess_game(source, requester_id, name, answer)
    return jsonify({"status": "success", "text": text})


@app.route('/guessrank', methods=['GET'])
def api_guess_rank():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    text = guess_rank_text()
    return jsonify({"status": "success", "text": text, "rank": MUSIC_STATS.guess_rank()})


@app.route('/undercover', methods=['GET'])
def api_undercover():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    source = request.args.get("source", "game")
    requester_id = request.args.get("id", "")
    name = request.args.get("name", requester_id or "玩家")
    action = request.args.get("action", "help")
    room = request.args.get("room", "")
    arg = request.args.get("arg", "")
    if not requester_id:
        return api_error("缺少用户 ID")
    return jsonify(UNDERCOVER.payload(source, requester_id, name, action, room, arg))


@app.route('/turtle_soup', methods=['POST'])
def api_turtle_soup():
    if not qqbot_server_authorized():
        return api_error("服务端鉴权失败", 401)
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")
    data = request.get_json(silent=True) or {}
    source = data.get("source", "game")
    requester_id = data.get("id", "")
    name = data.get("name", requester_id or "玩家")
    message = data.get("message", "")
    if not requester_id:
        return api_error("缺少用户 ID")
    return jsonify(TURTLE_SOUP.handle(source, requester_id, name, message))


@app.route('/download', methods=['POST'])
def api_download_compatible():
    if not request.is_json:
        return api_error("请求格式错误，请使用 JSON 请求体")
    
    data = request.get_json(silent=True) or {}
    page_url = data.get('page_url')
    title = data.get('title')
    artist = data.get('artist')
    map_name = data.get('map_name')
    target_map_path = data.get('target_map_path')

    if not all([page_url, title, artist]):
        return api_error("请求体中缺少 page_url/title/artist 参数")
    
    song_id = page_url
    print(f"[API /download] 收到下载请求: {title} - {artist} (ID: {song_id})")
    
    success, result_data = _perform_download_and_conversion(
        song_id, title, artist, NETEASE_COOKIES
    )
    
    if success:
        map_result = {}
        if map_name:
            map_success, map_result = prepare_music_map(song_id, map_name, target_map_path)
            if not map_success:
                print(f"[地图] 生成音乐地图失败: {map_result.get('message', '未知错误')}")
                if map_result.get("tool_output"):
                    print(f"[地图] 工具输出:\n{map_result['tool_output']}")
                return api_error(map_result.get("message", "音乐地图生成失败"), 500, **{
                    key: value for key, value in map_result.items() if key != "message"
                })

        print("[响应] 返回成功信息及歌曲时长。")
        response_payload = {
            "status": "success",
            "message": result_data.get("message"),
            "duration": result_data.get("duration", 0)
        }
        response_payload.update(map_result)
        return jsonify(response_payload)
    else:
        print("[响应] 返回失败信息。")
        return api_error(result_data.get("message", "下载失败"), 500, **{
            key: value for key, value in result_data.items() if key != "message"
        })

@app.route('/upload_map', methods=['POST'])
def api_upload_map():
    if not request.is_json:
        return jsonify({"success": False, "error": "请求格式错误，请使用 JSON 请求体"}), 400

    data = request.get_json(silent=True) or {}
    map_name = data.get('map_name')
    hash_value = data.get('hash')

    if not all([map_name, hash_value]):
        return jsonify({"success": False, "error": "请求体中缺少参数..."}), 400
        
    print(f"[API /upload_map] 收到上传请求，地图名: {map_name}, 哈希: {hash_value}")

    safe_map_name = os.path.basename(map_name)
    safe_hash_value = os.path.basename(hash_value)
    file_name = f"{safe_map_name}_{safe_hash_value}.map"
    local_file_path = CONFIG.webmaps_base_path / file_name

    if not local_file_path.exists():
        error_msg = f"文件未找到: {local_file_path}"
        print(f"[错误] {error_msg}")
        return jsonify({"success": False, "error": error_msg}), 404

    if not CONFIG.s3_ready():
        error_msg = "对象存储配置不完整，请检查 BPMUSIC_S3_* 环境变量"
        print(f"[错误] {error_msg}")
        return jsonify({"success": False, "error": error_msg}), 500

    upload_success, final_url, upload_error = UPLOAD_WEBMAP(file_name, local_file_path)
    if upload_success:
        success_msg = f"成功将 '{local_file_path}' 上传到 {final_url}"
        print(f"[成功] {success_msg}")
        return jsonify({"success": True, "url": final_url})

    print(f"[错误] {upload_error}")
    return jsonify({"success": False, "error": upload_error}), 500

# --- 4. 主程序入口 ---
if __name__ == '__main__':
    CONFIG.download_dir.mkdir(parents=True, exist_ok=True)
    CONFIG.cover_dir.mkdir(parents=True, exist_ok=True)
    CONFIG.webmaps_base_path.mkdir(parents=True, exist_ok=True)
    CONFIG.prepared_maps_dir.mkdir(parents=True, exist_ok=True)
    
    NETEASE_COOKIES = load_cookies()
    if not NETEASE_COOKIES:
        print("[初始化] 未找到本地 Cookies 文件，需要进行首次登录。")
        NETEASE_COOKIES = login_and_get_cookies()
        if not NETEASE_COOKIES:
            print("[致命错误] 未能获取 Cookies，API 服务无法启动。程序退出。")
            exit(1)
    else:
        print("[初始化] 成功从本地加载 Cookies。")

    ffmpeg_path = configure_audio_tools()
    start_qqbot_bridge()

    print("\n===================================================")
    print(" DDNet听歌房 辅助脚本 已启动")
    print("---------------------------------------------------")
    print(" > 音乐下载功能: 已激活 (支持返回时长)")
    print(" > 地图上传功能: 已激活")
    print(f" > FFmpeg: {'已找到 ' + ffmpeg_path if ffmpeg_path else '未找到，请安装并加入 PATH'}")
    print(f" > 地图缝合工具: {map_patcher_binary() or '未找到，请重新构建 game-server'}")
    print(f" > 对象存储: {'已配置' if CONFIG.s3_ready() else '未配置，仅本地播放可用'}")
    print(f" > QQ Bot: {'已启用' if QQBOT_BRIDGE.config.enabled else '未启用'}")
    print("---------------------------------------------------")
    print(f" 监听地址: http://{CONFIG.host}:{CONFIG.port}")
    print(f" 健康检查: http://{CONFIG.host}:{CONFIG.port}/health")
    print(f" Opus/LRC 目录: {CONFIG.download_dir.resolve()}")
    print(f" 专辑封面目录: {CONFIG.cover_dir.resolve()}")
    print(f" 原始地图目录: {CONFIG.origin_maps_dir.resolve()}")
    print(f" 候选地图目录: {CONFIG.prepared_maps_dir.resolve()}")
    print(f" Web 地图目录: {CONFIG.webmaps_base_path.resolve()}")
    print("===================================================\n")
    
    app.run(host=CONFIG.host, port=CONFIG.port, debug=False)
