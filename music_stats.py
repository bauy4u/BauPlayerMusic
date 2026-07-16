# -*- coding: utf-8 -*-

import hashlib
import secrets
import sqlite3
import threading
import time
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path


class MusicStatsStore:
    def __init__(self, database_path):
        self.database_path = Path(database_path)
        self._lock = threading.Lock()
        self._initialize()

    @contextmanager
    def _connection(self):
        connection = sqlite3.connect(self.database_path, timeout=10)
        try:
            connection.row_factory = sqlite3.Row
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("PRAGMA busy_timeout=10000")
            yield connection
            connection.commit()
        finally:
            connection.close()

    def _initialize(self):
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        with self._lock, self._connection() as connection:
            connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS music_requests (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    song_id TEXT NOT NULL,
                    title TEXT NOT NULL,
                    artist TEXT NOT NULL,
                    requester_source TEXT NOT NULL,
                    requester_id TEXT NOT NULL,
                    requester_name TEXT NOT NULL,
                    requested_at INTEGER NOT NULL,
                    requested_day TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_music_requests_requester
                    ON music_requests(requester_source, requester_id);
                CREATE INDEX IF NOT EXISTS idx_music_requests_song
                    ON music_requests(song_id);
                CREATE INDEX IF NOT EXISTS idx_music_requests_day
                    ON music_requests(requested_day);

                CREATE TABLE IF NOT EXISTS identity_bindings (
                    qq_id TEXT PRIMARY KEY,
                    game_name TEXT NOT NULL,
                    game_name_key TEXT NOT NULL UNIQUE,
                    qq_name TEXT NOT NULL,
                    role TEXT NOT NULL DEFAULT 'user',
                    bound_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS identity_bind_codes (
                    code_hash TEXT PRIMARY KEY,
                    game_name TEXT NOT NULL,
                    game_name_key TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_identity_bind_codes_game
                    ON identity_bind_codes(game_name_key);
                CREATE INDEX IF NOT EXISTS idx_identity_bind_codes_expiry
                    ON identity_bind_codes(expires_at);

                CREATE TABLE IF NOT EXISTS music_guess_wins (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    requester_source TEXT NOT NULL,
                    requester_id TEXT NOT NULL,
                    requester_name TEXT NOT NULL,
                    song_id TEXT NOT NULL,
                    title TEXT NOT NULL,
                    artist TEXT NOT NULL,
                    won_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS idx_music_guess_wins_requester
                    ON music_guess_wins(requester_source, requester_id);
                CREATE INDEX IF NOT EXISTS idx_music_guess_wins_song
                    ON music_guess_wins(song_id);
                """
            )

    @staticmethod
    def _game_name_key(game_name):
        return str(game_name or "").strip().casefold()

    @staticmethod
    def _code_hash(code):
        return hashlib.sha256(str(code or "").strip().upper().encode("utf-8")).hexdigest()

    def create_bind_code(self, game_name, ttl_seconds=300, timestamp=None):
        game_name = str(game_name or "").strip()
        game_name_key = self._game_name_key(game_name)
        if not game_name_key:
            raise ValueError("游戏昵称不能为空")
        timestamp = int(time.time()) if timestamp is None else int(timestamp)
        expires_at = timestamp + max(60, min(int(ttl_seconds), 1800))
        alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ"
        code = "".join(secrets.choice(alphabet) for _ in range(8))
        with self._lock, self._connection() as connection:
            connection.execute("DELETE FROM identity_bind_codes WHERE expires_at <= ?", (timestamp,))
            connection.execute(
                "DELETE FROM identity_bind_codes WHERE game_name_key = ?",
                (game_name_key,),
            )
            connection.execute(
                """
                INSERT INTO identity_bind_codes (
                    code_hash, game_name, game_name_key, created_at, expires_at
                ) VALUES (?, ?, ?, ?, ?)
                """,
                (self._code_hash(code), game_name, game_name_key, timestamp, expires_at),
            )
        return {"code": code, "game_name": game_name, "expires_at": expires_at}

    def consume_bind_code(self, code, qq_id, qq_name, timestamp=None):
        timestamp = int(time.time()) if timestamp is None else int(timestamp)
        qq_id = str(qq_id or "").strip()
        qq_name = str(qq_name or qq_id).strip()
        if not qq_id:
            return {"success": False, "message": "无法识别 QQ 账号。"}
        with self._lock, self._connection() as connection:
            connection.execute("DELETE FROM identity_bind_codes WHERE expires_at <= ?", (timestamp,))
            entry = connection.execute(
                """
                SELECT game_name, game_name_key, expires_at
                FROM identity_bind_codes WHERE code_hash = ?
                """,
                (self._code_hash(code),),
            ).fetchone()
            if not entry:
                return {"success": False, "message": "验证码无效或已过期，请回游戏重新生成。"}

            conflict = connection.execute(
                """
                SELECT qq_id FROM identity_bindings
                WHERE game_name_key = ? AND qq_id <> ?
                """,
                (entry["game_name_key"], qq_id),
            ).fetchone()
            if conflict:
                return {"success": False, "message": "这个游戏昵称已经绑定其他 QQ。"}

            existing = connection.execute(
                "SELECT bound_at, role FROM identity_bindings WHERE qq_id = ?",
                (qq_id,),
            ).fetchone()
            bound_at = existing["bound_at"] if existing else timestamp
            role = existing["role"] if existing else "user"
            connection.execute(
                """
                INSERT INTO identity_bindings (
                    qq_id, game_name, game_name_key, qq_name, role, bound_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(qq_id) DO UPDATE SET
                    game_name = excluded.game_name,
                    game_name_key = excluded.game_name_key,
                    qq_name = excluded.qq_name,
                    updated_at = excluded.updated_at
                """,
                (
                    qq_id,
                    entry["game_name"],
                    entry["game_name_key"],
                    qq_name,
                    role,
                    bound_at,
                    timestamp,
                ),
            )
            connection.execute(
                "DELETE FROM identity_bind_codes WHERE code_hash = ?",
                (self._code_hash(code),),
            )
        return {
            "success": True,
            "message": f"绑定成功：QQ {qq_name} ↔ 游戏昵称 {entry['game_name']}",
            "binding": self.binding_for_qq(qq_id),
        }

    def binding_for_qq(self, qq_id):
        with self._lock, self._connection() as connection:
            row = connection.execute(
                "SELECT * FROM identity_bindings WHERE qq_id = ?",
                (str(qq_id),),
            ).fetchone()
        return dict(row) if row else None

    def binding_for_game(self, game_name):
        with self._lock, self._connection() as connection:
            row = connection.execute(
                "SELECT * FROM identity_bindings WHERE game_name_key = ?",
                (self._game_name_key(game_name),),
            ).fetchone()
        return dict(row) if row else None

    def unbind_qq(self, qq_id):
        qq_id = str(qq_id or "").strip()
        if not qq_id:
            return {"success": False, "message": "无法识别 QQ 账号。"}
        with self._lock, self._connection() as connection:
            row = connection.execute(
                "SELECT game_name FROM identity_bindings WHERE qq_id = ?",
                (qq_id,),
            ).fetchone()
            if not row:
                return {"success": False, "message": "你还没有绑定游戏角色。"}
            connection.execute("DELETE FROM identity_bindings WHERE qq_id = ?", (qq_id,))
        return {"success": True, "message": f"已解除 QQ 与游戏昵称 {row['game_name']} 的绑定。"}

    def record(self, song_id, title, artist, requester_source, requester_id, requester_name, timestamp=None):
        timestamp = int(datetime.now(timezone.utc).timestamp()) if timestamp is None else int(timestamp)
        day = datetime.fromtimestamp(timestamp, tz=timezone.utc).astimezone().date().isoformat()
        with self._lock, self._connection() as connection:
            cursor = connection.execute(
                """
                INSERT INTO music_requests (
                    song_id, title, artist, requester_source, requester_id,
                    requester_name, requested_at, requested_day
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    str(song_id),
                    str(title),
                    str(artist),
                    str(requester_source or "game"),
                    str(requester_id or requester_name or "unknown"),
                    str(requester_name or "未知点歌者"),
                    timestamp,
                    day,
                ),
            )
            return cursor.lastrowid

    def summary(self, limit=5):
        today = datetime.now().astimezone().date().isoformat()
        with self._lock, self._connection() as connection:
            total = connection.execute("SELECT COUNT(*) FROM music_requests").fetchone()[0]
            today_total = connection.execute(
                "SELECT COUNT(*) FROM music_requests WHERE requested_day = ?", (today,)
            ).fetchone()[0]
            requesters = connection.execute(
                """
                SELECT requester_name, requester_source, COUNT(*) AS count
                FROM (
                    SELECT
                        COALESCE(game_binding.game_name, qq_binding.game_name, requests.requester_name)
                            AS requester_name,
                        CASE
                            WHEN game_binding.qq_id IS NOT NULL THEN 'bound:' || game_binding.qq_id
                            WHEN qq_binding.qq_id IS NOT NULL THEN 'bound:' || qq_binding.qq_id
                            ELSE requests.requester_source || ':' || requests.requester_id
                        END AS identity_key,
                        CASE
                            WHEN game_binding.qq_id IS NOT NULL OR qq_binding.qq_id IS NOT NULL
                                THEN 'bound'
                            ELSE requests.requester_source
                        END AS requester_source,
                        requests.requested_at
                    FROM music_requests AS requests
                    LEFT JOIN identity_bindings AS game_binding
                        ON requests.requester_source = 'game'
                        AND game_binding.game_name_key = lower(requests.requester_id)
                    LEFT JOIN identity_bindings AS qq_binding
                        ON requests.requester_source = 'qq'
                        AND qq_binding.qq_id = requests.requester_id
                )
                GROUP BY identity_key
                ORDER BY count DESC, MAX(requested_at) ASC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
            songs = connection.execute(
                """
                SELECT song_id, title, artist, COUNT(*) AS count
                FROM music_requests
                GROUP BY song_id
                ORDER BY count DESC, MAX(requested_at) ASC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return {
            "total": total,
            "today": today_total,
            "top_requesters": [dict(row) for row in requesters],
            "top_songs": [dict(row) for row in songs],
        }

    def personal(self, requester_source, requester_id, limit=5):
        binding = (
            self.binding_for_qq(requester_id)
            if str(requester_source) == "qq"
            else self.binding_for_game(requester_id)
            if str(requester_source) == "game"
            else None
        )
        identities = [(str(requester_source), str(requester_id))]
        if binding:
            identities = [
                ("game", binding["game_name"]),
                ("qq", binding["qq_id"]),
            ]
        where = " OR ".join("(requester_source = ? AND requester_id = ?)" for _ in identities)
        params = [value for identity in identities for value in identity]
        with self._lock, self._connection() as connection:
            total = connection.execute(
                f"SELECT COUNT(*) FROM music_requests WHERE {where}",
                params,
            ).fetchone()[0]
            songs = connection.execute(
                f"""
                SELECT song_id, title, artist, COUNT(*) AS count
                FROM music_requests
                WHERE {where}
                GROUP BY song_id
                ORDER BY count DESC, MAX(requested_at) ASC
                LIMIT ?
                """,
                (*params, limit),
            ).fetchall()
        return {
            "total": total,
            "top_songs": [dict(row) for row in songs],
            "binding": binding,
        }

    def record_guess_win(self, requester_source, requester_id, requester_name, song_id, title, artist, timestamp=None):
        timestamp = int(time.time()) if timestamp is None else int(timestamp)
        with self._lock, self._connection() as connection:
            cursor = connection.execute(
                """
                INSERT INTO music_guess_wins (
                    requester_source, requester_id, requester_name,
                    song_id, title, artist, won_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    str(requester_source),
                    str(requester_id),
                    str(requester_name),
                    str(song_id),
                    str(title),
                    str(artist),
                    timestamp,
                ),
            )
            return cursor.lastrowid

    def guess_rank(self, limit=10):
        with self._lock, self._connection() as connection:
            rows = connection.execute(
                """
                SELECT requester_name, requester_source, COUNT(*) AS wins, MAX(won_at) AS last_win_at
                FROM (
                    SELECT
                        COALESCE(game_binding.game_name, qq_binding.game_name, wins.requester_name)
                            AS requester_name,
                        CASE
                            WHEN game_binding.qq_id IS NOT NULL THEN 'bound:' || game_binding.qq_id
                            WHEN qq_binding.qq_id IS NOT NULL THEN 'bound:' || qq_binding.qq_id
                            ELSE wins.requester_source || ':' || wins.requester_id
                        END AS identity_key,
                        CASE
                            WHEN game_binding.qq_id IS NOT NULL OR qq_binding.qq_id IS NOT NULL
                                THEN 'bound'
                            ELSE wins.requester_source
                        END AS requester_source,
                        wins.won_at
                    FROM music_guess_wins AS wins
                    LEFT JOIN identity_bindings AS game_binding
                        ON wins.requester_source = 'game'
                        AND game_binding.game_name_key = lower(wins.requester_id)
                    LEFT JOIN identity_bindings AS qq_binding
                        ON wins.requester_source = 'qq'
                        AND qq_binding.qq_id = wins.requester_id
                )
                GROUP BY identity_key
                ORDER BY wins DESC, last_win_at ASC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        return [dict(row) for row in rows]


def format_music_rank(summary):
    lines = [f"点歌统计：今日 {summary['today']} 次，累计 {summary['total']} 次"]
    if summary["top_requesters"]:
        lines.append("点歌达人")
        for index, entry in enumerate(summary["top_requesters"], start=1):
            lines.append(f"{index}. {entry['requester_name']} - {entry['count']} 次")
    if summary["top_songs"]:
        lines.append("热门歌曲")
        for index, entry in enumerate(summary["top_songs"], start=1):
            lines.append(f"{index}. {entry['title']} - {entry['artist']} ({entry['count']} 次)")
    return "\n".join(lines)


def format_personal_rank(name, personal):
    if personal["total"] <= 0:
        return f"{name} 暂无点歌记录。"
    lines = [f"{name} 累计点歌 {personal['total']} 次"]
    if personal["top_songs"]:
        lines.append("最常点")
        for index, entry in enumerate(personal["top_songs"], start=1):
            lines.append(f"{index}. {entry['title']} - {entry['artist']} ({entry['count']} 次)")
    return "\n".join(lines)


def format_guess_rank(entries):
    if not entries:
        return "猜歌榜暂无记录。"
    lines = ["猜歌榜"]
    for index, entry in enumerate(entries, start=1):
        lines.append(f"{index}. {entry['requester_name']} - {entry['wins']} 次")
    return "\n".join(lines)
