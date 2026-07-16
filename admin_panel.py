#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Local Web administration panel for BauPlayerMusic.

The panel intentionally runs as a separate process from the DDNet server and
music backend. It only listens on loopback by default and never returns stored
secret values to the browser.
"""

from __future__ import annotations

import argparse
import hmac
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import webbrowser
from dataclasses import dataclass
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from flask import Flask, jsonify, request, send_from_directory


PROJECT_ROOT = Path(__file__).resolve().parent
WEB_ROOT = PROJECT_ROOT / "admin_web"
ENV_PATH = PROJECT_ROOT / ".env"
ENV_EXAMPLE_PATH = PROJECT_ROOT / ".env.example"
SERVER_CONFIG_PATH = PROJECT_ROOT / "myServerconfig.cfg"
SERVER_CONFIG_EXAMPLE_PATH = PROJECT_ROOT / "myServerconfig.example.cfg"
STATE_DIR = PROJECT_ROOT / "data" / "admin_panel"
PROCESS_STATE_PATH = STATE_DIR / "processes.json"
LOG_DIR = STATE_DIR / "logs"


ENV_SCHEMA = [
    {
        "id": "backend",
        "title": "音乐后端",
        "description": "监听地址、端口和地图处理路径。",
        "fields": [
            ("BPMUSIC_HOST", "监听地址", "text", "127.0.0.1", "仅本机使用时保持 127.0.0.1。"),
            ("BPMUSIC_PORT", "监听端口", "number", "5000", "必须与服务端后端 URL 一致。"),
            ("BPMUSIC_NETEASE_BASE_URL", "网易云接口", "url", "http://music.163.com", "通常无需修改。"),
            ("BPMUSIC_DOWNLOAD_DIR", "音乐目录", "text", "data/musicso", "Opus、歌词和封面保存位置。"),
            ("BPMUSIC_ORIGIN_MAPS_DIR", "原始地图目录", "text", "data/originmaps", "未缝入音乐的地图。"),
            ("BPMUSIC_PREPARED_MAPS_DIR", "候选地图目录", "text", "data/musico/prepared_maps", "运行时生成，不应提交 Git。"),
            ("BPMUSIC_MAP_PATCHER", "地图缝合器", "text", "", "留空时自动寻找 music_map_patcher.exe。"),
            ("BPMUSIC_UPLOAD_WORKERS", "并发任务数", "number", "2", "小型服务器建议 2。"),
        ],
    },
    {
        "id": "qqbot",
        "title": "QQBot / NapCat",
        "description": "OneBot WebSocket、群号和消息转发限制。",
        "fields": [
            ("BPMUSIC_QQBOT_ENABLED", "启用 QQBot", "switch", "0", "修改后重启音乐后端。"),
            ("BPMUSIC_QQBOT_WS_URL", "WebSocket 地址", "url", "ws://127.0.0.1:3001", "NapCat OneBot 11 WebSocket 服务端地址。"),
            ("BPMUSIC_QQBOT_ACCESS_TOKEN", "NapCat Access Token", "secret", "", "必须与 NapCat 网络配置一致。"),
            ("BPMUSIC_QQBOT_GROUP_IDS", "允许群号", "text", "", "多个群号使用英文逗号分隔。"),
            ("BPMUSIC_QQBOT_SERVER_TOKEN", "服务端共享密钥", "secret", "", "与 sv_music_qq_token 一致，建议至少 64 位。"),
            ("BPMUSIC_QQBOT_MAX_SONGS", "最大歌曲数", "number", "10", "QQ 查询队列时最多返回的项目数。"),
            ("BPMUSIC_QQBOT_RECONNECT_SECONDS", "重连间隔", "number", "5", "单位：秒。"),
            ("BPMUSIC_QQBOT_RELAY_GLOBAL_COOLDOWN", "全局转发冷却", "number", "5", "单位：秒。"),
            ("BPMUSIC_QQBOT_RELAY_USER_COOLDOWN", "用户转发冷却", "number", "30", "单位：秒。"),
        ],
    },
    {
        "id": "turtle",
        "title": "海龟汤",
        "description": "题库、数据库和可选的 DeepSeek 自由问答。",
        "fields": [
            ("BPMUSIC_TURTLE_SOUP_CASES", "题库文件", "text", "data/musico/turtle_soup_cases.json", "JSON 题库路径。"),
            ("BPMUSIC_TURTLE_SOUP_DB", "进度数据库", "text", "data/musico/turtle_soup.sqlite3", "运行时自动创建。"),
            ("DEEPSEEK_API_KEY", "DeepSeek API Key", "secret", "", "不填也可使用本地题库逻辑。"),
            ("DEEPSEEK_API_BASE", "API 地址", "url", "https://api.deepseek.com", "兼容接口基础地址。"),
            ("DEEPSEEK_MODEL", "模型", "text", "deepseek-v4-pro", "按实际账号可用模型填写。"),
            ("TURTLE_SOUP_PLAYER_COOLDOWN", "玩家冷却", "number", "4", "单位：秒。"),
            ("TURTLE_SOUP_GLOBAL_COOLDOWN", "全局冷却", "number", "0.8", "单位：秒。"),
        ],
    },
    {
        "id": "storage",
        "title": "对象存储",
        "description": "S3、雨云或兼容对象存储的内网上传配置。",
        "fields": [
            ("BPMUSIC_S3_ENDPOINT_URL", "API 端点", "url", "", "雨云内网示例：https://cn-nb1.internal.rains3.com"),
            ("BPMUSIC_S3_ACCESS_KEY", "Access Key", "secret", "", "只保存在服务器本地 .env。"),
            ("BPMUSIC_S3_SECRET_KEY", "Secret Key", "secret", "", "不会回传到浏览器。"),
            ("BPMUSIC_S3_BUCKET_NAME", "桶名称", "text", "", "例如 maps。"),
            ("BPMUSIC_S3_PUBLIC_DOMAIN", "公网访问域名", "url", "", "供玩家下载地图，不能填写内网端点。"),
            ("BPMUSIC_S3_OBJECT_ACL", "对象 ACL", "text", "", "通常留空；确有需要时填 public-read。"),
            ("BPMUSIC_WEBMAPS_BASE_PATH", "本地地图缓存", "text", "data/webmaps", "上传前的本地缓存目录。"),
            ("BPMUSIC_ALIYUN_OSS_ENDPOINT", "阿里云 OSS 端点", "url", "", "仅 mds_aliyun.py 使用。"),
        ],
    },
    {
        "id": "panel",
        "title": "管理面板",
        "description": "面板本身的监听和访问保护，修改后重启面板。",
        "fields": [
            ("BPMUSIC_PANEL_HOST", "面板监听地址", "text", "127.0.0.1", "强烈建议保持 127.0.0.1。"),
            ("BPMUSIC_PANEL_PORT", "面板端口", "number", "8787", "本机浏览器访问端口。"),
            ("BPMUSIC_PANEL_TOKEN", "面板访问密钥", "secret", "", "绑定非本机地址时强制要求设置。"),
        ],
    },
]

SERVER_SCHEMA = [
    ("sv_name", "服务器名称", "text", "BauPlayerMusic", "显示在服务器列表中的名称。"),
    ("sv_map", "默认地图", "text", "a", "仓库默认地图为 a.map。"),
    ("sv_port", "游戏端口", "number", "8303", "公网需放行对应 UDP 端口。"),
    ("sv_max_clients", "最大玩家数", "number", "64", "按服务器性能调整。"),
    ("sv_register", "注册到公网列表", "switch", "0", "正式开放前保持关闭。"),
    ("sv_rcon_password", "RCON 管理密码", "secret", "", "至少 24 位且不要与其他密码复用。"),
    ("sv_music_backend_url", "音乐后端 URL", "url", "http://127.0.0.1:5000", "与 BPMUSIC_HOST/PORT 对应。"),
    ("sv_music_qq_relay", "QQ 消息转发", "switch", "0", "启用前完成 QQBot 配置。"),
    ("sv_music_qq_token", "QQ 共享密钥", "secret", "", "与 BPMUSIC_QQBOT_SERVER_TOKEN 完全一致。"),
    ("sv_maps_base_url", "地图下载前缀", "url", "", "对象存储公网域名，末尾建议保留斜杠。"),
]

ENV_KEYS = {field[0] for section in ENV_SCHEMA for field in section["fields"]}
ENV_SECRET_KEYS = {field[0] for section in ENV_SCHEMA for field in section["fields"] if field[2] == "secret"}
SERVER_KEYS = {field[0] for field in SERVER_SCHEMA}
SERVER_SECRET_KEYS = {field[0] for field in SERVER_SCHEMA if field[2] == "secret"}


def _strip_quotes(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    return value


def _dotenv_value(value: Any) -> str:
    text = str(value).replace("\r", "").replace("\n", "\\n")
    if not text:
        return ""
    if re.search(r"\s|#|[\"']", text):
        return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return text


class EnvDocument:
    def __init__(self, path: Path = ENV_PATH, example_path: Path = ENV_EXAMPLE_PATH):
        self.path = path
        self.example_path = example_path

    def ensure(self) -> None:
        if not self.path.exists():
            if not self.example_path.exists():
                raise FileNotFoundError("找不到 .env 和 .env.example")
            self.path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.example_path, self.path)

    def read(self) -> dict[str, str]:
        self.ensure()
        values: dict[str, str] = {}
        for line in self.path.read_text(encoding="utf-8-sig").splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#") or "=" not in stripped:
                continue
            key, value = stripped.split("=", 1)
            key = key.removeprefix("export ").strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
                values[key] = _strip_quotes(value)
        return values

    def update(self, updates: dict[str, Any], clear_secrets: set[str] | None = None) -> None:
        self.ensure()
        clear_secrets = clear_secrets or set()
        allowed_updates: dict[str, str] = {}
        current = self.read()
        for key, value in updates.items():
            if key not in ENV_KEYS:
                continue
            if key in ENV_SECRET_KEYS and (value is None or str(value) == "") and key not in clear_secrets:
                continue
            allowed_updates[key] = "" if key in clear_secrets else str(value)
        if not allowed_updates:
            return

        lines = self.path.read_text(encoding="utf-8-sig").splitlines()
        seen: set[str] = set()
        output: list[str] = []
        pattern = re.compile(r"^(\s*)(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)(\s*)=")
        for line in lines:
            match = pattern.match(line)
            if match and match.group(2) in allowed_updates:
                key = match.group(2)
                output.append(f"{key}={_dotenv_value(allowed_updates[key])}")
                seen.add(key)
            else:
                output.append(line)
        for key, value in allowed_updates.items():
            if key not in seen:
                output.append(f"{key}={_dotenv_value(value)}")
        temp = self.path.with_suffix(self.path.suffix + ".tmp")
        temp.write_text("\n".join(output).rstrip() + "\n", encoding="utf-8")
        os.replace(temp, self.path)


def _cfg_value(value: Any, field_type: str) -> str:
    text = str(value).replace("\r", " ").replace("\n", " ").strip()
    if field_type in {"number", "switch"}:
        return text or "0"
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


class ServerConfigDocument:
    def __init__(self, path: Path = SERVER_CONFIG_PATH, example_path: Path = SERVER_CONFIG_EXAMPLE_PATH):
        self.path = path
        self.example_path = example_path

    def ensure(self) -> None:
        if not self.path.exists():
            if not self.example_path.exists():
                raise FileNotFoundError("找不到 myServerconfig.cfg 和示例文件")
            shutil.copy2(self.example_path, self.path)

    def read(self) -> dict[str, str]:
        self.ensure()
        values: dict[str, str] = {}
        for line in self.path.read_text(encoding="utf-8-sig").splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            parts = stripped.split(None, 1)
            if len(parts) == 2 and parts[0] in SERVER_KEYS:
                values[parts[0]] = _strip_quotes(parts[1])
        return values

    def update(self, updates: dict[str, Any], clear_secrets: set[str] | None = None) -> None:
        self.ensure()
        clear_secrets = clear_secrets or set()
        types = {field[0]: field[2] for field in SERVER_SCHEMA}
        allowed: dict[str, str] = {}
        for key, value in updates.items():
            if key not in SERVER_KEYS:
                continue
            if key in SERVER_SECRET_KEYS and (value is None or str(value) == "") and key not in clear_secrets:
                continue
            allowed[key] = "" if key in clear_secrets else str(value)
        if not allowed:
            return
        lines = self.path.read_text(encoding="utf-8-sig").splitlines()
        seen: set[str] = set()
        output: list[str] = []
        for line in lines:
            stripped = line.strip()
            key = stripped.split(None, 1)[0] if stripped and not stripped.startswith("#") else ""
            if key in allowed:
                output.append(f"{key} {_cfg_value(allowed[key], types[key])}")
                seen.add(key)
            else:
                output.append(line)
        for key, value in allowed.items():
            if key not in seen:
                output.append(f"{key} {_cfg_value(value, types[key])}")
        temp = self.path.with_suffix(self.path.suffix + ".tmp")
        temp.write_text("\n".join(output).rstrip() + "\n", encoding="utf-8")
        os.replace(temp, self.path)


ENV_DOCUMENT = EnvDocument()
SERVER_DOCUMENT = ServerConfigDocument()


def load_env_values() -> dict[str, str]:
    try:
        return ENV_DOCUMENT.read()
    except FileNotFoundError:
        return {}


def resolve_project_path(value: str, default: str) -> Path:
    path = Path(value or default)
    return path if path.is_absolute() else PROJECT_ROOT / path


def http_json(url: str, timeout: float = 2.5) -> tuple[bool, Any]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return True, json.loads(response.read().decode("utf-8"))
    except (OSError, urllib.error.URLError, ValueError) as error:
        return False, str(error)


def backend_health(values: dict[str, str] | None = None) -> tuple[bool, Any]:
    values = values or load_env_values()
    host = values.get("BPMUSIC_HOST") or "127.0.0.1"
    if host in {"0.0.0.0", "::"}:
        host = "127.0.0.1"
    port = values.get("BPMUSIC_PORT") or "5000"
    return http_json(f"http://{host}:{port}/health")


def tcp_check(host: str, port: int, timeout: float = 2.0) -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True, f"{host}:{port} 可连接"
    except OSError as error:
        return False, f"{host}:{port} 无法连接：{error}"


def is_pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def process_image_running(image_name: str) -> bool:
    if os.name != "nt":
        return False
    try:
        output = subprocess.check_output(
            ["tasklist", "/FI", f"IMAGENAME eq {image_name}", "/FO", "CSV", "/NH"],
            text=True,
            encoding="utf-8",
            errors="ignore",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return image_name.lower() in output.lower()
    except (OSError, subprocess.SubprocessError):
        return False


@dataclass
class ManagedProcess:
    name: str
    command: list[str]
    cwd: Path
    log_path: Path
    pid: int = 0


class ProcessManager:
    def __init__(self) -> None:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self._state = self._load_state()

    def _load_state(self) -> dict[str, dict[str, Any]]:
        try:
            data = json.loads(PROCESS_STATE_PATH.read_text(encoding="utf-8"))
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def _save_state(self) -> None:
        temp = PROCESS_STATE_PATH.with_suffix(".tmp")
        temp.write_text(json.dumps(self._state, ensure_ascii=False, indent=2), encoding="utf-8")
        os.replace(temp, PROCESS_STATE_PATH)

    def managed_pid(self, name: str) -> int:
        with self._lock:
            pid = int(self._state.get(name, {}).get("pid", 0) or 0)
            if pid and is_pid_alive(pid):
                return pid
            if name in self._state:
                self._state.pop(name, None)
                self._save_state()
            return 0

    def status(self, name: str, detect_external: bool = True) -> dict[str, Any]:
        pid = self.managed_pid(name)
        external = False
        if detect_external and name == "backend":
            external = backend_health()[0]
        elif detect_external and name == "server":
            external = process_image_running("DDNet-Server.exe")
        return {
            "running": bool(pid or external),
            "managed": bool(pid),
            "pid": pid or None,
            "external": bool(external and not pid),
        }

    def _definition(self, name: str) -> ManagedProcess:
        if name == "backend":
            return ManagedProcess(name, [sys.executable, "-u", str(PROJECT_ROOT / "mds.py")], PROJECT_ROOT, LOG_DIR / "backend.log")
        if name == "server":
            exe = find_server_executable()
            if not exe:
                raise FileNotFoundError("未找到 DDNet-Server.exe，请先编译或把面板放到部署目录")
            SERVER_DOCUMENT.ensure()
            if exe.parent != PROJECT_ROOT:
                shutil.copy2(SERVER_CONFIG_PATH, exe.parent / SERVER_CONFIG_PATH.name)
            return ManagedProcess(name, [str(exe)], exe.parent, LOG_DIR / "server.log")
        raise ValueError("未知服务")

    def start(self, name: str) -> dict[str, Any]:
        with self._lock:
            status = self.status(name)
            if status["running"]:
                if status["external"]:
                    raise RuntimeError("该服务已在面板外运行；为避免误杀进程，面板不会接管")
                return status
            definition = self._definition(name)
            definition.log_path.parent.mkdir(parents=True, exist_ok=True)
            log_file = definition.log_path.open("a", encoding="utf-8", buffering=1)
            log_file.write(f"\n[{time.strftime('%Y-%m-%d %H:%M:%S')}] panel starting {name}\n")
            flags = 0
            if os.name == "nt":
                flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) | getattr(subprocess, "CREATE_NO_WINDOW", 0)
            process = subprocess.Popen(
                definition.command,
                cwd=definition.cwd,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                creationflags=flags,
                close_fds=os.name != "nt",
            )
            log_file.close()
            self._state[name] = {"pid": process.pid, "started_at": int(time.time()), "log": str(definition.log_path)}
            self._save_state()
            time.sleep(0.25)
            return self.status(name)

    def stop(self, name: str) -> dict[str, Any]:
        with self._lock:
            pid = self.managed_pid(name)
            if not pid:
                if self.status(name)["external"]:
                    raise RuntimeError("服务不是由面板启动，拒绝直接结束外部进程")
                return self.status(name)
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(pid), "/T", "/F"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                    check=False,
                )
            else:
                try:
                    os.kill(pid, 15)
                except OSError:
                    pass
            self._state.pop(name, None)
            self._save_state()
            return self.status(name)

    def restart(self, name: str) -> dict[str, Any]:
        status = self.status(name)
        if status["external"]:
            raise RuntimeError("服务不是由面板启动，无法安全重启")
        if status["managed"]:
            self.stop(name)
            time.sleep(0.4)
        return self.start(name)


PROCESS_MANAGER = ProcessManager()


def find_server_executable() -> Path | None:
    candidates = [
        PROJECT_ROOT / "DDNet-Server.exe",
        PROJECT_ROOT / "build-server" / "Release" / "DDNet-Server.exe",
        PROJECT_ROOT / "build" / "Release" / "DDNet-Server.exe",
        PROJECT_ROOT / "out" / "build" / "x64-Release" / "DDNet-Server.exe",
    ]
    return next((path for path in candidates if path.is_file()), None)


def schema_payload() -> dict[str, Any]:
    def fields_payload(fields: list[tuple[str, str, str, str, str]]) -> list[dict[str, str]]:
        return [
            {"key": key, "label": label, "type": field_type, "default": default, "help": help_text}
            for key, label, field_type, default, help_text in fields
        ]

    return {
        "env": [
            {**{key: value for key, value in section.items() if key != "fields"}, "fields": fields_payload(section["fields"])}
            for section in ENV_SCHEMA
        ],
        "server": fields_payload(SERVER_SCHEMA),
    }


def public_values(values: dict[str, str], secret_keys: set[str], schema_keys: set[str]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key in schema_keys:
        value = values.get(key, "")
        result[key] = {"value": "" if key in secret_keys else value, "configured": bool(value) if key in secret_keys else None}
    return result


def turtle_status(values: dict[str, str]) -> dict[str, Any]:
    cases_path = resolve_project_path(values.get("BPMUSIC_TURTLE_SOUP_CASES", ""), "data/musico/turtle_soup_cases.json")
    result = {"path": str(cases_path), "exists": cases_path.is_file(), "cases": 0, "error": "", "deepseek": bool(values.get("DEEPSEEK_API_KEY"))}
    if cases_path.is_file():
        try:
            data = json.loads(cases_path.read_text(encoding="utf-8-sig"))
            if isinstance(data, list):
                result["cases"] = len(data)
            elif isinstance(data, dict):
                for key in ("cases", "items", "data"):
                    if isinstance(data.get(key), list):
                        result["cases"] = len(data[key])
                        break
        except (OSError, ValueError) as error:
            result["error"] = str(error)
    return result


def directory_summary(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"path": str(path), "exists": False, "files": 0, "bytes": 0}
    files = 0
    total = 0
    try:
        for item in path.rglob("*"):
            if item.is_file():
                files += 1
                total += item.stat().st_size
    except OSError:
        pass
    return {"path": str(path), "exists": True, "files": files, "bytes": total}


def status_payload() -> dict[str, Any]:
    values = load_env_values()
    health_ok, health = backend_health(values)
    turtle = turtle_status(values)
    download_dir = resolve_project_path(values.get("BPMUSIC_DOWNLOAD_DIR", ""), "data/musicso")
    prepared_dir = resolve_project_path(values.get("BPMUSIC_PREPARED_MAPS_DIR", ""), "data/musico/prepared_maps")
    endpoint = values.get("BPMUSIC_S3_ENDPOINT_URL", "")
    s3_keys = ["BPMUSIC_S3_ENDPOINT_URL", "BPMUSIC_S3_ACCESS_KEY", "BPMUSIC_S3_SECRET_KEY", "BPMUSIC_S3_BUCKET_NAME", "BPMUSIC_S3_PUBLIC_DOMAIN"]
    backend_process = PROCESS_MANAGER.status("backend", detect_external=False)
    if health_ok and not backend_process["managed"]:
        backend_process.update({"running": True, "external": True})
    map_patcher = resolve_project_path(values.get("BPMUSIC_MAP_PATCHER", ""), "music_map_patcher.exe")
    map_path = PROJECT_ROOT / "data" / "maps" / "a.map"
    font_path = PROJECT_ROOT / "data" / "fonts" / "SourceHanSans.ttc"
    return {
        "time": int(time.time()),
        "services": {"backend": backend_process, "server": PROCESS_MANAGER.status("server")},
        "backend": {"online": health_ok, "health": health if health_ok else None, "error": "" if health_ok else str(health)},
        "qqbot": (health.get("qqbot") if health_ok and isinstance(health, dict) else {"enabled": values.get("BPMUSIC_QQBOT_ENABLED", "0") in {"1", "true", "yes", "on"}, "connected": False}),
        "turtle": turtle,
        "storage": {"configured": all(values.get(key) for key in s3_keys), "endpoint": endpoint, "bucket": values.get("BPMUSIC_S3_BUCKET_NAME", "")},
        "runtime": {
            "python": sys.version.split()[0],
            "ffmpeg": shutil.which("ffmpeg") or shutil.which("ffmpeg.exe"),
            "map_patcher": str(map_patcher) if map_patcher.is_file() else "",
            "server_exe": str(find_server_executable() or ""),
            "map": str(map_path) if map_path.is_file() else "",
            "font": str(font_path) if font_path.is_file() else "",
        },
        "usage": {"music": directory_summary(download_dir), "prepared_maps": directory_summary(prepared_dir)},
    }


def tail_log(name: str, max_lines: int = 250) -> str:
    paths = {
        "backend": LOG_DIR / "backend.log",
        "server": LOG_DIR / "server.log",
        "ddnet": PROJECT_ROOT / "autoexec_server.log",
    }
    path = paths.get(name)
    if not path or not path.is_file():
        return "暂无日志。"
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        return "\n".join(lines[-max(20, min(max_lines, 1000)):])
    except OSError as error:
        return f"读取日志失败：{error}"


app = Flask(__name__, static_folder=None)
PANEL_TOKEN = ""


@app.before_request
def authorize_api():
    if not request.path.startswith("/api/") or not PANEL_TOKEN:
        return None
    provided = request.headers.get("X-Panel-Token", "")
    if not hmac.compare_digest(provided, PANEL_TOKEN):
        return jsonify({"ok": False, "error": "需要管理面板访问密钥"}), 401
    return None


@app.get("/")
def index():
    return send_from_directory(WEB_ROOT, "index.html")


@app.get("/assets/<path:name>")
def assets(name: str):
    return send_from_directory(WEB_ROOT, name)


@app.get("/api/bootstrap")
def api_bootstrap():
    env_values = ENV_DOCUMENT.read()
    server_values = SERVER_DOCUMENT.read()
    return jsonify({
        "ok": True,
        "schema": schema_payload(),
        "settings": {
            "env": public_values(env_values, ENV_SECRET_KEYS, ENV_KEYS),
            "server": public_values(server_values, SERVER_SECRET_KEYS, SERVER_KEYS),
        },
        "status": status_payload(),
        "auth": bool(PANEL_TOKEN),
    })


@app.get("/api/status")
def api_status():
    return jsonify({"ok": True, "status": status_payload()})


@app.post("/api/settings")
def api_save_settings():
    payload = request.get_json(silent=True) or {}
    env_updates = payload.get("env") if isinstance(payload.get("env"), dict) else {}
    server_updates = payload.get("server") if isinstance(payload.get("server"), dict) else {}
    clear = payload.get("clearSecrets") if isinstance(payload.get("clearSecrets"), list) else []
    clear_set = {str(key) for key in clear}
    ENV_DOCUMENT.update(env_updates, clear_set & ENV_SECRET_KEYS)
    SERVER_DOCUMENT.update(server_updates, clear_set & SERVER_SECRET_KEYS)
    return jsonify({"ok": True, "message": "配置已安全写入；相关服务需重启后生效"})


@app.post("/api/service/<name>/<action>")
def api_service(name: str, action: str):
    if name not in {"backend", "server"} or action not in {"start", "stop", "restart"}:
        return jsonify({"ok": False, "error": "不支持的服务操作"}), 404
    try:
        result = getattr(PROCESS_MANAGER, action)(name)
        return jsonify({"ok": True, "service": result, "message": f"{name} {action} 操作完成"})
    except (OSError, RuntimeError, ValueError) as error:
        return jsonify({"ok": False, "error": str(error)}), 409


@app.post("/api/check/<name>")
def api_check(name: str):
    values = load_env_values()
    if name == "backend":
        ok, result = backend_health(values)
        return jsonify({"ok": ok, "message": "音乐后端在线" if ok else f"音乐后端不可用：{result}", "details": result if ok else None})
    if name == "qqbot":
        parsed = urlparse(values.get("BPMUSIC_QQBOT_WS_URL", "ws://127.0.0.1:3001"))
        if not parsed.hostname:
            return jsonify({"ok": False, "message": "WebSocket 地址无效"})
        ok, message = tcp_check(parsed.hostname, parsed.port or (443 if parsed.scheme == "wss" else 80))
        health_ok, health = backend_health(values)
        details = health.get("qqbot") if health_ok and isinstance(health, dict) else None
        return jsonify({"ok": ok, "message": message, "details": details})
    if name == "turtle":
        result = turtle_status(values)
        ok = bool(result["exists"] and not result["error"] and result["cases"])
        message = f"题库有效，共 {result['cases']} 道题" if ok else (result["error"] or "题库不存在或没有可用题目")
        return jsonify({"ok": ok, "message": message, "details": result})
    if name == "storage":
        required = ["BPMUSIC_S3_ENDPOINT_URL", "BPMUSIC_S3_ACCESS_KEY", "BPMUSIC_S3_SECRET_KEY", "BPMUSIC_S3_BUCKET_NAME"]
        missing = [key for key in required if not values.get(key)]
        if missing:
            return jsonify({"ok": False, "message": "缺少配置：" + ", ".join(missing)})
        try:
            import boto3
            from botocore.config import Config

            client = boto3.client(
                "s3",
                endpoint_url=values["BPMUSIC_S3_ENDPOINT_URL"],
                aws_access_key_id=values["BPMUSIC_S3_ACCESS_KEY"],
                aws_secret_access_key=values["BPMUSIC_S3_SECRET_KEY"],
                config=Config(connect_timeout=3, read_timeout=5, retries={"max_attempts": 1}),
            )
            client.head_bucket(Bucket=values["BPMUSIC_S3_BUCKET_NAME"])
            return jsonify({"ok": True, "message": "对象存储连接和桶权限正常"})
        except Exception as error:  # boto providers expose many service-specific exceptions
            return jsonify({"ok": False, "message": f"对象存储检查失败：{error}"})
    return jsonify({"ok": False, "message": "未知检查项目"}), 404


@app.get("/api/logs/<name>")
def api_logs(name: str):
    if name not in {"backend", "server", "ddnet"}:
        return jsonify({"ok": False, "error": "未知日志"}), 404
    try:
        lines = int(request.args.get("lines", "250"))
    except ValueError:
        lines = 250
    return jsonify({"ok": True, "name": name, "content": tail_log(name, lines)})


def parse_args() -> argparse.Namespace:
    values = load_env_values()
    parser = argparse.ArgumentParser(description="BauPlayerMusic local administration panel")
    parser.add_argument("--host", default=values.get("BPMUSIC_PANEL_HOST") or "127.0.0.1")
    parser.add_argument("--port", type=int, default=int(values.get("BPMUSIC_PANEL_PORT") or "8787"))
    parser.add_argument("--open-browser", action="store_true")
    return parser.parse_args()


def main() -> None:
    global PANEL_TOKEN
    ENV_DOCUMENT.ensure()
    SERVER_DOCUMENT.ensure()
    values = load_env_values()
    args = parse_args()
    PANEL_TOKEN = os.environ.get("BPMUSIC_PANEL_TOKEN") or values.get("BPMUSIC_PANEL_TOKEN", "")
    if args.host not in {"127.0.0.1", "localhost", "::1"} and not PANEL_TOKEN:
        raise SystemExit("拒绝启动：面板绑定非本机地址时必须配置 BPMUSIC_PANEL_TOKEN")
    url_host = "127.0.0.1" if args.host in {"0.0.0.0", "::"} else args.host
    url = f"http://{url_host}:{args.port}/"
    if args.open_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    print(f"BauPlayerMusic 管理面板：{url}")
    print("默认仅本机可访问；按 Ctrl+C 退出面板，不会自动停止游戏服务。")
    app.run(host=args.host, port=args.port, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()
