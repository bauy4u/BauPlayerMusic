# -*- coding: utf-8 -*-

import base64
import hashlib
import json
import os
import random
import re
import threading
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit


def _env(name, default=""):
    value = os.environ.get(name)
    return value if value not in (None, "") else default


def _env_bool(name, default=False):
    value = _env(name)
    if not value:
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _env_int(name, default, minimum, maximum):
    try:
        value = int(_env(name, str(default)))
    except ValueError:
        value = default
    return max(minimum, min(maximum, value))


def _default_playlist_file(project_root):
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "DDNet" / "data" / "musico" / "playlist.txt"
    return Path(project_root) / "data" / "musico" / "playlist.txt"


@dataclass(frozen=True)
class QQBotConfig:
    enabled: bool
    websocket_url: str
    access_token: str
    group_ids: frozenset
    playlist_file: Path
    max_songs: int
    reconnect_seconds: int
    server_token: str
    relay_global_cooldown: int
    relay_user_cooldown: int
    relay_max_length: int

    @classmethod
    def from_env(cls, project_root):
        raw_group_ids = _env("BPMUSIC_QQBOT_GROUP_IDS")
        group_ids = frozenset(
            int(value.strip())
            for value in raw_group_ids.split(",")
            if value.strip().isdigit()
        )
        playlist_file = _env("BPMUSIC_PLAYLIST_FILE")
        return cls(
            enabled=_env_bool("BPMUSIC_QQBOT_ENABLED"),
            websocket_url=_env("BPMUSIC_QQBOT_WS_URL", "ws://127.0.0.1:3001"),
            access_token=_env("BPMUSIC_QQBOT_ACCESS_TOKEN"),
            group_ids=group_ids,
            playlist_file=Path(playlist_file) if playlist_file else _default_playlist_file(project_root),
            max_songs=_env_int("BPMUSIC_QQBOT_MAX_SONGS", 10, 1, 20),
            reconnect_seconds=_env_int("BPMUSIC_QQBOT_RECONNECT_SECONDS", 5, 1, 300),
            server_token=_env("BPMUSIC_QQBOT_SERVER_TOKEN"),
            relay_global_cooldown=_env_int("BPMUSIC_QQBOT_RELAY_GLOBAL_COOLDOWN", 5, 0, 3600),
            relay_user_cooldown=_env_int("BPMUSIC_QQBOT_RELAY_USER_COOLDOWN", 30, 0, 3600),
            relay_max_length=_env_int("BPMUSIC_QQBOT_RELAY_MAX_LENGTH", 160, 1, 500),
        )


def _split_playlist_line(line):
    fields = []
    field = []
    escaped = False
    for character in line:
        if escaped:
            field.append("\n" if character == "n" else "\r" if character == "r" else character)
            escaped = False
        elif character == "\\":
            escaped = True
        elif character == "|":
            fields.append("".join(field))
            field = []
        else:
            field.append(character)
    if escaped:
        field.append("\\")
    fields.append("".join(field))
    return fields


def read_playlist_snapshot(path, attempts=4):
    path = Path(path)
    last_error = None
    for attempt in range(attempts):
        try:
            before = path.stat()
            content = path.read_text(encoding="utf-8")
            after = path.stat()
            if before.st_mtime_ns != after.st_mtime_ns or before.st_size != after.st_size:
                raise RuntimeError("播放队列文件正在更新")

            lines = content.splitlines()
            if not lines:
                raise RuntimeError("播放队列文件为空")

            state_fields = _split_playlist_line(lines[0])
            if len(state_fields) != 6 or state_fields[0] != "STATE":
                raise RuntimeError("播放队列状态行不完整")

            snapshot = {
                "current_index": int(state_fields[1]),
                "start_time": int(state_fields[2]),
                "duration": float(state_fields[3]),
                "is_playing": state_fields[4] == "true",
                "songs": [],
            }
            for line in lines[1:]:
                fields = _split_playlist_line(line)
                if len(fields) < 6:
                    raise RuntimeError("播放队列歌曲行不完整")
                snapshot["songs"].append({
                    "title": fields[0],
                    "artist": fields[1],
                    "song_id": fields[2],
                    "duration": float(fields[3]),
                    "is_preloaded": fields[4] == "true",
                    "is_ready": fields[5] == "true",
                    "requester_name": fields[6] if len(fields) >= 9 else "未知",
                    "requester_source": fields[7] if len(fields) >= 9 else "legacy",
                    "requester_id": fields[8] if len(fields) >= 9 else "legacy",
                })
            return snapshot
        except (OSError, ValueError, RuntimeError) as error:
            last_error = error
            if attempt + 1 < attempts:
                time.sleep(0.03)
    raise RuntimeError(f"读取播放队列失败: {last_error}")


def _format_duration(seconds):
    seconds = max(0, int(seconds))
    return f"{seconds // 60:02d}:{seconds % 60:02d}"


def format_playlist_message(snapshot, max_songs=10, now=None):
    songs = snapshot["songs"]
    if not songs:
        return "听歌服播放队列为空。"

    now = int(time.time()) if now is None else int(now)
    current_index = snapshot["current_index"]
    lines = ["听歌服播放队列"]

    if snapshot["is_playing"] and 0 <= current_index < len(songs):
        current = songs[current_index]
        elapsed = max(0, now - snapshot["start_time"]) if snapshot["start_time"] > 0 else 0
        duration = current["duration"] or snapshot["duration"]
        if duration > 0:
            elapsed = min(elapsed, int(duration))
            progress = f" {_format_duration(elapsed)}/{_format_duration(duration)}"
        else:
            progress = ""
        lines.append(f"正在播放: {current['title']} - {current['artist']}{progress}")
    else:
        lines.append("当前尚未开始播放")

    for index, song in enumerate(songs[:max_songs], start=1):
        marker = " [正在播放]" if index - 1 == current_index and snapshot["is_playing"] else ""
        lines.append(
            f"{index}. {song['title']} - {song['artist']} | 点歌: {song['requester_name']}{marker}"
        )

    remaining = len(songs) - max_songs
    if remaining > 0:
        lines.append(f"... 还有 {remaining} 首")
    return "\n".join(lines)


def daily_luck_text(name, identity_key, now=None):
    today = time.strftime("%Y%m%d", time.localtime(time.time() if now is None else now))
    digest = hashlib.sha256(f"{today}|{identity_key}".encode("utf-8")).digest()
    score = digest[0] % 101
    if score >= 95:
        comment = "今天离谱地闪，适合点一首压轴曲。"
    elif score >= 80:
        comment = "气势很好，排队点歌大概率很顺。"
    elif score >= 60:
        comment = "稳稳的，不惊不险，适合循环喜欢的歌。"
    elif score >= 35:
        comment = "普通但耐听，今天主打慢慢来。"
    elif score >= 15:
        comment = "有点卡拍，建议先喝口水再点歌。"
    else:
        comment = "今天别硬刚，交给 Bot 做选择题吧。"
    return f"{name} 今日人品：{score}/100\n{comment}"


def roll_text(argument):
    text = str(argument or "").strip().lower()
    match = re.fullmatch(r"(?:(\d{1,2})d)?(\d{1,4})", text or "100")
    if not match:
        return "用法：/roll [面数] 或 /roll 2d6，范围：最多 20 个骰子，每个 2-1000 面。"
    count = int(match.group(1) or "1")
    sides = int(match.group(2))
    if count < 1 or count > 20 or sides < 2 or sides > 1000:
        return "骰子范围：最多 20 个骰子，每个 2-1000 面。"
    rolls = [random.randint(1, sides) for _ in range(count)]
    if count == 1:
        return f"掷出 d{sides}：{rolls[0]}"
    return f"掷出 {count}d{sides}：{' + '.join(str(value) for value in rolls)} = {sum(rolls)}"


def pick_text(argument):
    text = str(argument or "").strip()
    if not text:
        return "用法：/pick 选项A | 选项B | 选项C"
    normalized = re.sub(r"[，,、/]+", "|", text)
    options = [option.strip() for option in normalized.split("|") if option.strip()]
    if len(options) < 2:
        options = [option.strip() for option in re.split(r"\s+", text) if option.strip()]
    if len(options) < 2:
        return "至少给两个选项，比如：/pick 红茶 | 咖啡"
    if len(options) > 20:
        return "选择太多啦，最多 20 个。"
    choice = random.choice(options)
    return f"我选：{choice}"


class QQBotBridge:
    def __init__(self, config):
        self.config = config
        self._thread = None
        self._websocket = None
        self._connected = False
        self._last_error = ""
        self._self_id = None
        self._relay_lock = threading.Lock()
        self._incoming_messages = deque(maxlen=100)
        self._last_incoming_relay = 0.0
        self._incoming_user_cooldowns = {}
        self._seen_message_ids = set()
        self._seen_message_order = deque(maxlen=200)
        self._server_status_provider = None
        self._players_provider = None
        self._music_rank_provider = None
        self._personal_rank_provider = None
        self._bind_provider = None
        self._profile_provider = None
        self._ddnet_provider = None
        self._identity_provider = None
        self._unbind_provider = None
        self._guess_provider = None
        self._guess_rank_provider = None
        self._command_global_cooldowns = {}
        self._command_user_cooldowns = {}
        self._shared_relay_global = 0.0
        self._shared_relay_cooldowns = {}

    def set_command_providers(
        self,
        server_status,
        players,
        music_rank,
        personal_rank,
        bind_identity=None,
        identity_profile=None,
        ddnet_profile=None,
        identity_context=None,
        unbind_identity=None,
        guess_game=None,
        guess_rank=None,
    ):
        self._server_status_provider = server_status
        self._players_provider = players
        self._music_rank_provider = music_rank
        self._personal_rank_provider = personal_rank
        self._bind_provider = bind_identity
        self._profile_provider = identity_profile
        self._ddnet_provider = ddnet_profile
        self._identity_provider = identity_context
        self._unbind_provider = unbind_identity
        self._guess_provider = guess_game
        self._guess_rank_provider = guess_rank

    def start(self):
        if not self.config.enabled:
            return False
        if not self.config.group_ids:
            self._last_error = "未配置 BPMUSIC_QQBOT_GROUP_IDS，QQ Bot 未启动"
            print(f"[QQBot] {self._last_error}")
            return False
        if self._thread and self._thread.is_alive():
            return True

        self._thread = threading.Thread(target=self._run, name="qqbot-onebot", daemon=True)
        self._thread.start()
        return True

    def status(self):
        return {
            "enabled": self.config.enabled,
            "connected": self._connected,
            "groups": sorted(self.config.group_ids),
            "playlist_file": str(self.config.playlist_file.resolve()),
            "last_error": self._last_error or None,
            "pending_server_messages": len(self._incoming_messages),
        }

    @staticmethod
    def sanitize_relay_text(value, max_length):
        text = re.sub(r"[\x00-\x1f\x7f]+", " ", str(value or ""))
        text = re.sub(r"\s+", " ", text).strip()
        return text[:max_length]

    def send_server_message(self, player_name, text):
        if not self.config.enabled:
            return False, "QQ Bot 未启用"
        if not self._connected or not self._websocket:
            return False, "QQ Bot 尚未连接"

        player_name = self.sanitize_relay_text(player_name, 32) or "匿名玩家"
        text = self.sanitize_relay_text(text, self.config.relay_max_length)
        if not text:
            return False, "喊话内容为空"
        identity = self._identity_context("game", player_name, player_name)
        cooldown_message = self._consume_shared_relay_cooldown(identity["key"])
        if cooldown_message:
            return False, cooldown_message
        player_name = identity["display_name"]

        templates = (
            "{player} 在听歌服里向群友喊话：{text}",
            "来自听歌服的传声筒：{player} 说：{text}",
            "听歌服现场消息，{player} 托我带话：{text}",
            "{player} 从听歌服发来一条群聊广播：{text}",
            "跨服连线接通，听歌服的 {player} 说道：{text}",
        )
        relay_text = random.choice(templates).format(player=player_name, text=text)
        try:
            for group_id in self.config.group_ids:
                self._send_group_text(group_id, relay_text, echo=f"bpmusic-server-{time.time_ns()}")
        except Exception as error:
            self._last_error = self._safe_error(error)
            return False, f"发送群消息失败: {self._last_error}"
        return True, relay_text

    def _identity_context(self, source, identity_id, display_name):
        if self._identity_provider:
            context = self._identity_provider(source, str(identity_id), display_name)
            if isinstance(context, dict) and context.get("key"):
                return context
        return {
            "key": f"{source}:{str(identity_id).casefold()}",
            "display_name": display_name,
            "role": "user",
        }

    def _consume_shared_relay_cooldown(self, identity_key):
        now = time.monotonic()
        with self._relay_lock:
            global_left = self.config.relay_global_cooldown - (now - self._shared_relay_global)
            user_last = self._shared_relay_cooldowns.get(identity_key, 0.0)
            user_left = self.config.relay_user_cooldown - (now - user_last)
            seconds_left = max(global_left, user_left)
            if seconds_left > 0:
                return f"喊话冷却中，请等待 {int(seconds_left) + 1} 秒。"
            self._shared_relay_global = now
            self._shared_relay_cooldowns[identity_key] = now
        return None

    def poll_server_messages(self, limit=10):
        messages = []
        with self._relay_lock:
            while self._incoming_messages and len(messages) < limit:
                messages.append(self._incoming_messages.popleft())
        return messages

    def _safe_error(self, error):
        message = str(error)
        if self.config.access_token:
            message = message.replace(self.config.access_token, "***")
        return message

    def _run(self):
        try:
            import websocket
        except ImportError:
            self._last_error = "缺少 websocket-client，请执行 python -m pip install -r requirements.txt"
            print(f"[QQBot] {self._last_error}")
            return

        headers = []
        if self.config.access_token:
            headers.append(f"Authorization: Bearer {self.config.access_token}")
        websocket_url = self._websocket_url()

        while True:
            try:
                self._websocket = websocket.WebSocketApp(
                    websocket_url,
                    header=headers,
                    on_open=self._on_open,
                    on_message=self._on_message,
                    on_error=self._on_error,
                    on_close=self._on_close,
                )
                self._websocket.run_forever(ping_interval=30, ping_timeout=10)
            except Exception as error:
                self._last_error = self._safe_error(error)
                print(f"[QQBot] OneBot 连接异常: {self._last_error}")
            self._connected = False
            time.sleep(self.config.reconnect_seconds)

    def _websocket_url(self):
        if not self.config.access_token:
            return self.config.websocket_url
        parts = urlsplit(self.config.websocket_url)
        query = dict(parse_qsl(parts.query, keep_blank_values=True))
        query.setdefault("access_token", self.config.access_token)
        return urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query), parts.fragment))

    def _on_open(self, websocket_app):
        self._connected = True
        self._last_error = ""
        print(f"[QQBot] 已连接 OneBot: {self.config.websocket_url}")
        websocket_app.send(json.dumps({"action": "get_login_info", "echo": "bpmusic-login-info"}))

    def _on_error(self, _websocket_app, error):
        self._last_error = self._safe_error(error)
        print(f"[QQBot] OneBot 错误: {self._last_error}")

    def _on_close(self, _websocket_app, status_code, message):
        self._connected = False
        print(f"[QQBot] OneBot 连接关闭 ({status_code}): {message or '无附加信息'}")

    def _on_message(self, websocket_app, raw_message):
        try:
            event = json.loads(raw_message)
        except (TypeError, json.JSONDecodeError):
            return

        if event.get("echo") == "bpmusic-login-info":
            data = event.get("data") or {}
            self._self_id = str(data.get("user_id")) if data.get("user_id") is not None else None
            if self._self_id:
                print(f"[QQBot] 当前登录 QQ: {self._self_id}")
            return

        if event.get("post_type") != "message" or event.get("message_type") != "group":
            return
        if str(event.get("user_id")) == str(event.get("self_id")):
            return

        try:
            group_id = int(event.get("group_id"))
        except (TypeError, ValueError):
            return
        if group_id not in self.config.group_ids:
            return

        self_id = str(event.get("self_id") or self._self_id or "")
        if not self_id:
            return

        command, argument = self._extract_command(event.get("message"), self_id)
        if command == "help" and not argument:
            self._reply_group_event(websocket_app, event, self._help_text())
        elif command == "jrrp" and not argument:
            sender = event.get("sender") or {}
            fallback_name = self.sanitize_relay_text(
                sender.get("card") or sender.get("nickname") or str(event.get("user_id")),
                32,
            )
            identity = self._identity_context("qq", str(event.get("user_id")), fallback_name)
            self._reply_group_event(
                websocket_app,
                event,
                daily_luck_text(identity["display_name"], identity["key"]),
            )
        elif command == "roll":
            self._reply_group_event(websocket_app, event, roll_text(argument))
        elif command == "pick":
            self._reply_group_event(websocket_app, event, pick_text(argument))
        elif command == "guess":
            sender = event.get("sender") or {}
            nickname = self.sanitize_relay_text(
                sender.get("card") or sender.get("nickname") or str(event.get("user_id")),
                32,
            )
            reply_text = (
                self._guess_provider("qq", str(event.get("user_id")), nickname, argument)
                if self._guess_provider else "猜歌功能暂不可用。"
            )
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "guessrank" and not argument:
            reply_text = self._guess_rank_provider() if self._guess_rank_provider else "猜歌榜暂不可用。"
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "mls" and not argument:
            try:
                snapshot = read_playlist_snapshot(self.config.playlist_file)
                reply_text = format_playlist_message(snapshot, self.config.max_songs)
            except RuntimeError as error:
                reply_text = str(error)
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "say":
            self._handle_group_relay(websocket_app, event, argument)
        elif command == "status" and not argument:
            reply_text = self._server_status_provider() if self._server_status_provider else "服务器状态暂不可用。"
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "players" and not argument:
            reply_text = self._players_provider() if self._players_provider else "玩家列表暂不可用。"
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "musicrank" and not argument:
            reply_text = self._music_rank_provider() if self._music_rank_provider else "点歌排行暂不可用。"
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "myrank" and not argument:
            sender = event.get("sender") or {}
            nickname = self.sanitize_relay_text(
                sender.get("card") or sender.get("nickname") or str(event.get("user_id")),
                32,
            )
            reply_text = (
                self._personal_rank_provider("qq", str(event.get("user_id")), nickname)
                if self._personal_rank_provider else "个人点歌统计暂不可用。"
            )
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "bind":
            if not argument:
                self._reply_group_event(websocket_app, event, "用法：@机器人 /bind 游戏内生成的验证码")
                return
            sender = event.get("sender") or {}
            nickname = self.sanitize_relay_text(
                sender.get("card") or sender.get("nickname") or str(event.get("user_id")),
                32,
            )
            reply_text = (
                self._bind_provider(str(event.get("user_id")), nickname, argument)
                if self._bind_provider else "身份绑定功能暂不可用。"
            )
            self._reply_group_event(websocket_app, event, reply_text)
        elif command == "unbind" and not argument:
            reply_text = (
                self._unbind_provider(str(event.get("user_id")))
                if self._unbind_provider else "解绑功能暂不可用。"
            )
            self._reply_group_event(websocket_app, event, reply_text)
        elif command in {"me", "profile"} and not argument:
            if not self._profile_provider:
                self._reply_group_event(websocket_app, event, "角色资料暂不可用。")
                return
            self._run_provider_async(
                websocket_app,
                event,
                self._profile_provider,
                str(event.get("user_id")),
            )
        elif command == "ddnet":
            if not self._ddnet_provider:
                self._reply_group_event(websocket_app, event, "DDNet 资料查询暂不可用。")
                return
            cooldown_message = self._consume_command_cooldown("ddnet", str(event.get("user_id")), 15, 60)
            if cooldown_message:
                self._reply_group_event(websocket_app, event, cooldown_message)
                return
            self._run_provider_async(
                websocket_app,
                event,
                self._ddnet_provider,
                str(event.get("user_id")),
                argument,
            )

    @staticmethod
    def _help_text():
        return (
            "听歌服 Bot 指令\n"
            "/mls 播放队列\n"
            "/status 服务器状态\n"
            "/players 在线玩家\n"
            "/musicrank 点歌排行\n"
            "/myrank 我的点歌记录\n"
            "/bind 验证码 绑定游戏角色\n"
            "/unbind 解除绑定\n"
            "/me 我的听歌服资料\n"
            "/ddnet [玩家名] DDNet 官方资料截图\n"
            "/jrrp 今日人品\n"
            "/roll [面数|NdM] 掷骰子\n"
            "/pick A | B | C 帮你选择\n"
            "/guess [歌名] 猜当前歌\n"
            "/guessrank 猜歌榜\n"
            "/say 内容 向游戏内喊话"
        )

    def _consume_command_cooldown(self, command, user_id, global_seconds, user_seconds):
        now = time.monotonic()
        key = (command, str(user_id))
        with self._relay_lock:
            global_left = global_seconds - (now - self._command_global_cooldowns.get(command, 0.0))
            user_left = user_seconds - (now - self._command_user_cooldowns.get(key, 0.0))
            seconds_left = max(global_left, user_left)
            if seconds_left > 0:
                return f"查询冷却中，请等待 {int(seconds_left) + 1} 秒。"
            self._command_global_cooldowns[command] = now
            self._command_user_cooldowns[key] = now
        return None

    def _run_provider_async(self, websocket_app, event, provider, *args):
        event_copy = dict(event)

        def worker():
            try:
                result = provider(*args)
            except Exception as error:
                self._last_error = self._safe_error(error)
                result = "查询失败，请稍后重试。"
            try:
                self._reply_group_event(websocket_app, event_copy, result)
            except Exception as error:
                self._last_error = self._safe_error(error)

        threading.Thread(target=worker, name="qqbot-command", daemon=True).start()

    def _reply_group_event(self, websocket_app, event, text):
        image_path = None
        if isinstance(text, dict):
            image_path = text.get("image_path")
            text = text.get("text", "")
        message = []
        if event.get("message_id") is not None:
            message.append({"type": "reply", "data": {"id": str(event["message_id"])}})
        if text:
            message.append({"type": "text", "data": {"text": str(text)}})
        if image_path:
            image_data = base64.b64encode(Path(image_path).read_bytes()).decode("ascii")
            message.append({"type": "image", "data": {"file": f"base64://{image_data}"}})
        action = {
            "action": "send_group_msg",
            "params": {"group_id": str(event["group_id"]), "message": message},
            "echo": f"bpmusic-mls-{event.get('message_id', int(time.time()))}",
        }
        websocket_app.send(json.dumps(action, ensure_ascii=False))

    def _send_group_text(self, group_id, text, echo):
        action = {
            "action": "send_group_msg",
            "params": {
                "group_id": str(group_id),
                "message": [{"type": "text", "data": {"text": text}}],
            },
            "echo": echo,
        }
        self._websocket.send(json.dumps(action, ensure_ascii=False))

    def _handle_group_relay(self, websocket_app, event, argument):
        text = self.sanitize_relay_text(argument, self.config.relay_max_length)
        if not text:
            self._reply_group_event(websocket_app, event, "用法：@机器人 /say 内容")
            return

        now = time.monotonic()
        user_id = str(event.get("user_id"))
        message_id = str(event.get("message_id") or "")
        sender = event.get("sender") or {}
        qq_name = self.sanitize_relay_text(
            sender.get("card") or sender.get("nickname") or user_id,
            32,
        )
        identity = self._identity_context("qq", user_id, qq_name)
        shared_cooldown = self._consume_shared_relay_cooldown(identity["key"])
        if shared_cooldown:
            self._reply_group_event(websocket_app, event, shared_cooldown)
            return
        with self._relay_lock:
            if message_id and message_id in self._seen_message_ids:
                return
            global_left = self.config.relay_global_cooldown - (now - self._last_incoming_relay)
            user_last = self._incoming_user_cooldowns.get(user_id, 0.0)
            user_left = self.config.relay_user_cooldown - (now - user_last)
            seconds_left = max(global_left, user_left)
            if seconds_left > 0:
                cooldown_message = f"喊话冷却中，请等待 {int(seconds_left) + 1} 秒。"
            else:
                nickname = identity["display_name"]
                templates = (
                    "[QQ群] {nickname} 向听歌服喊话：{text}",
                    "来自QQ群的消息，{nickname} 说：{text}",
                    "群友 {nickname} 托机器人带话：{text}",
                    "QQ传声筒 | {nickname}：{text}",
                    "跨服连线接通，群里的 {nickname} 说道：{text}",
                )
                relay_text = random.choice(templates).format(nickname=nickname, text=text)
                self._incoming_messages.append({
                    "text": relay_text,
                    "message_id": message_id,
                    "group_id": str(event.get("group_id")),
                    "user_id": user_id,
                })
                self._last_incoming_relay = now
                self._incoming_user_cooldowns[user_id] = now
                if message_id:
                    if len(self._seen_message_order) == self._seen_message_order.maxlen:
                        self._seen_message_ids.discard(self._seen_message_order[0])
                    self._seen_message_order.append(message_id)
                    self._seen_message_ids.add(message_id)
                cooldown_message = None

        if cooldown_message:
            self._reply_group_event(websocket_app, event, cooldown_message)
        else:
            self._reply_group_event(websocket_app, event, "已转发到听歌服。")

    @staticmethod
    def _extract_command(message, self_id):
        if isinstance(message, list):
            mentioned = any(
                segment.get("type") == "at" and str((segment.get("data") or {}).get("qq")) == self_id
                for segment in message
                if isinstance(segment, dict)
            )
            text = "".join(
                str((segment.get("data") or {}).get("text", ""))
                for segment in message
                if isinstance(segment, dict) and segment.get("type") == "text"
            ).strip()
        elif isinstance(message, str):
            mentioned = re.search(rf"\[CQ:at,qq={re.escape(self_id)}(?:,[^\]]*)?\]", message) is not None
            text = re.sub(r"\[CQ:[^\]]+\]", "", message).strip()
        else:
            return None, ""

        if not mentioned:
            return None, ""
        match = re.fullmatch(
            r"/(help|mls|say|status|players|musicrank|myrank|bind|unbind|me|profile|ddnet|jrrp|roll|pick|guess|guessrank)(?:\s+([\s\S]*))?",
            text,
            flags=re.IGNORECASE,
        )
        if not match:
            return None, ""
        return match.group(1).lower(), (match.group(2) or "").strip()

    @staticmethod
    def _is_mls_request(message, self_id):
        command, argument = QQBotBridge._extract_command(message, self_id)
        return command == "mls" and not argument
