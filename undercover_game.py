import random
import re
import sqlite3
import threading
import time
from collections import Counter
from contextlib import contextmanager
from pathlib import Path


DEFAULT_WORD_PAIRS = [
    ("奶茶", "豆浆"),
    ("火锅", "麻辣烫"),
    ("耳机", "音响"),
    ("钢琴", "电子琴"),
    ("地铁", "公交车"),
    ("猫咖", "咖啡馆"),
    ("月亮", "星星"),
    ("烤肉", "烧烤"),
    ("冰箱", "空调"),
    ("雨伞", "雨衣"),
    ("手机", "平板"),
    ("电影院", "剧场"),
    ("雪碧", "七喜"),
    ("键盘", "手柄"),
    ("游泳", "潜水"),
    ("米饭", "炒饭"),
    ("医生", "护士"),
    ("飞机", "高铁"),
    ("书店", "图书馆"),
    ("炸鸡", "鸡排"),
    ("蛋糕", "面包"),
    ("相机", "望远镜"),
    ("森林", "公园"),
    ("香水", "洗发水"),
    ("外卖", "快递"),
    ("吉他", "贝斯"),
    ("篮球", "足球"),
    ("螺蛳粉", "酸辣粉"),
    ("枕头", "抱枕"),
    ("雪糕", "冰淇淋"),
]

MAX_ROOM_NAME_LENGTH = 24
MAX_PLAYER_NAME_LENGTH = 24
MAX_PLAYERS = 12
SPEECH_SECONDS = 45


def _now():
    return int(time.time())


def _clean_text(value, limit):
    text = re.sub(r"[\x00-\x1f\x7f]+", " ", str(value or "")).strip()
    return text[:limit]


def _normalize(value):
    return re.sub(r"\s+", "", str(value or "").casefold())


class UndercoverStore:
    def __init__(self, database_path, words_path=None):
        self.database_path = Path(database_path)
        self.words_path = Path(words_path) if words_path else None
        self.lock = threading.RLock()
        self._memory_database = str(database_path) == ":memory:"
        self._memory_connection = None
        if self._memory_database:
            self._memory_connection = sqlite3.connect(":memory:", timeout=30, check_same_thread=False)
            self._memory_connection.row_factory = sqlite3.Row
        else:
            self.database_path.parent.mkdir(parents=True, exist_ok=True)
        if self.words_path:
            self.words_path.parent.mkdir(parents=True, exist_ok=True)
        self._initialize()
        self._ensure_words_file()

    @contextmanager
    def _connection(self):
        conn = self._memory_connection if self._memory_database else sqlite3.connect(self.database_path, timeout=30)
        conn.row_factory = sqlite3.Row
        try:
            conn.execute("PRAGMA foreign_keys = ON")
            conn.execute("PRAGMA busy_timeout = 30000")
            if not self._memory_database:
                conn.execute("PRAGMA journal_mode = WAL")
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            if not self._memory_database:
                conn.close()

    def _initialize(self):
        with self._connection() as conn:
            conn.executescript(
                """
                CREATE TABLE IF NOT EXISTS uc_rooms (
                    room_id TEXT PRIMARY KEY,
                    owner_source TEXT NOT NULL,
                    owner_id TEXT NOT NULL,
                    owner_name TEXT NOT NULL,
                    status TEXT NOT NULL,
                    phase TEXT NOT NULL DEFAULT 'waiting',
                    round_no INTEGER NOT NULL DEFAULT 0,
                    civilian_word TEXT NOT NULL DEFAULT '',
                    undercover_word TEXT NOT NULL DEFAULT '',
                    current_speaker_source TEXT NOT NULL DEFAULT '',
                    current_speaker_id TEXT NOT NULL DEFAULT '',
                    current_speaker_name TEXT NOT NULL DEFAULT '',
                    speech_deadline INTEGER NOT NULL DEFAULT 0,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS uc_players (
                    room_id TEXT NOT NULL,
                    source TEXT NOT NULL,
                    player_id TEXT NOT NULL,
                    player_name TEXT NOT NULL,
                    role TEXT NOT NULL DEFAULT '',
                    word TEXT NOT NULL DEFAULT '',
                    alive INTEGER NOT NULL DEFAULT 1,
                    spoken INTEGER NOT NULL DEFAULT 0,
                    turn_order INTEGER NOT NULL DEFAULT 0,
                    joined_at INTEGER NOT NULL,
                    PRIMARY KEY(room_id, source, player_id),
                    FOREIGN KEY(room_id) REFERENCES uc_rooms(room_id)
                );
                CREATE TABLE IF NOT EXISTS uc_votes (
                    room_id TEXT NOT NULL,
                    voter_source TEXT NOT NULL,
                    voter_id TEXT NOT NULL,
                    target_source TEXT NOT NULL,
                    target_id TEXT NOT NULL,
                    PRIMARY KEY(room_id, voter_source, voter_id),
                    FOREIGN KEY(room_id) REFERENCES uc_rooms(room_id)
                );
                CREATE TABLE IF NOT EXISTS uc_stats (
                    source TEXT NOT NULL,
                    player_id TEXT NOT NULL,
                    player_name TEXT NOT NULL,
                    games INTEGER NOT NULL DEFAULT 0,
                    wins INTEGER NOT NULL DEFAULT 0,
                    undercover_wins INTEGER NOT NULL DEFAULT 0,
                    civilian_wins INTEGER NOT NULL DEFAULT 0,
                    updated_at INTEGER NOT NULL,
                    PRIMARY KEY(source, player_id)
                );
                """
            )
            self._ensure_column(conn, "uc_rooms", "phase", "TEXT NOT NULL DEFAULT 'waiting'")
            self._ensure_column(conn, "uc_rooms", "current_speaker_source", "TEXT NOT NULL DEFAULT ''")
            self._ensure_column(conn, "uc_rooms", "current_speaker_id", "TEXT NOT NULL DEFAULT ''")
            self._ensure_column(conn, "uc_rooms", "current_speaker_name", "TEXT NOT NULL DEFAULT ''")
            self._ensure_column(conn, "uc_rooms", "speech_deadline", "INTEGER NOT NULL DEFAULT 0")
            self._ensure_column(conn, "uc_players", "spoken", "INTEGER NOT NULL DEFAULT 0")
            self._ensure_column(conn, "uc_players", "turn_order", "INTEGER NOT NULL DEFAULT 0")
            self._repair_inconsistent_rooms(conn)

    def _ensure_column(self, conn, table, column, definition):
        rows = conn.execute(f"PRAGMA table_info({table})").fetchall()
        if any(row["name"] == column for row in rows):
            return
        conn.execute(f"ALTER TABLE {table} ADD COLUMN {column} {definition}")

    def _repair_inconsistent_rooms(self, conn):
        now = _now()
        conn.execute(
            """
            UPDATE uc_rooms
            SET status = 'finished', phase = 'finished', current_speaker_source = '',
                current_speaker_id = '', current_speaker_name = '', speech_deadline = 0,
                updated_at = ?
            WHERE status = 'playing'
              AND (
                phase NOT IN ('speaking', 'voting')
                OR (phase = 'speaking' AND current_speaker_id = '')
              )
            """,
            (now,),
        )
        conn.execute(
            """
            UPDATE uc_rooms
            SET phase = 'waiting', current_speaker_source = '', current_speaker_id = '',
                current_speaker_name = '', speech_deadline = 0, updated_at = ?
            WHERE status = 'waiting' AND phase != 'waiting'
            """,
            (now,),
        )

    def _ensure_words_file(self):
        if not self.words_path or self.words_path.exists():
            return
        lines = ["# 每行一个词组：平民词|卧底词"]
        lines.extend(f"{civilian}|{undercover}" for civilian, undercover in DEFAULT_WORD_PAIRS)
        self.words_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def _load_words(self):
        pairs = []
        if self.words_path and self.words_path.exists():
            for raw_line in self.words_path.read_text(encoding="utf-8").splitlines():
                line = raw_line.strip()
                if not line or line.startswith("#"):
                    continue
                if "|" in line:
                    left, right = line.split("|", 1)
                elif "," in line:
                    left, right = line.split(",", 1)
                else:
                    continue
                left = _clean_text(left, 32)
                right = _clean_text(right, 32)
                if left and right and left != right:
                    pairs.append((left, right))
        return pairs or list(DEFAULT_WORD_PAIRS)

    def handle(self, source, player_id, player_name, action="help", room="", arg=""):
        with self.lock:
            return self._handle(source, player_id, player_name, action, room, arg)

    def _handle(self, source, player_id, player_name, action="help", room="", arg=""):
        source = _clean_text(source or "game", 16) or "game"
        player_id = _clean_text(player_id, 64)
        player_name = _clean_text(player_name or player_id or "玩家", MAX_PLAYER_NAME_LENGTH)
        action = _normalize(action or "help")
        room = _clean_text(room, MAX_ROOM_NAME_LENGTH)
        arg = _clean_text(arg, 80)
        if not player_id:
            return "谁是卧底：缺少玩家 ID。"

        if action in {"", "help", "h", "帮助"}:
            return self.help_text()
        if action in {"create", "开房", "创建"}:
            return self.create_room(source, player_id, player_name, room)
        if action in {"join", "加入"}:
            return self.join_room(source, player_id, player_name, room)
        if action in {"leave", "离开"}:
            return self.leave_room(source, player_id, player_name)
        if action in {"list", "房间"}:
            return self.list_rooms()
        if action in {"players", "p", "玩家"}:
            return self.players_text(source, player_id)
        if action in {"start", "开始"}:
            return self.start_room(source, player_id)
        if action in {"word", "词", "我的词"}:
            return self.word_text(source, player_id)
        if action in {"vote", "投票"}:
            return self.vote(source, player_id, player_name, arg or room)
        if action in {"speak", "pass", "timeout", "发言", "跳过", "超时"}:
            return self.speak(source, player_id, player_name, action)
        if action in {"status", "状态"}:
            return self.status_text(source, player_id)
        if action in {"end", "close", "结束", "解散"}:
            return self.end_room(source, player_id)
        if action in {"rank", "排行", "排行榜"}:
            return self.rank_text()
        return "谁是卧底：未知命令。发送 /uc help 查看玩法。"

    def help_text(self):
        return (
            "谁是卧底 /uc\n"
            "/uc create 房间名：创建房间\n"
            "/uc join 房间名：加入房间\n"
            "/uc start：房主开始，3-12 人\n"
            "/uc word：私聊查看自己的词\n"
            "/uc pass：当前发言者跳过发言\n"
            "/uc vote 玩家名：投票淘汰\n"
            "/uc status / players / list / leave / end / rank"
        )

    def payload(self, source, player_id, player_name, action="help", room="", arg=""):
        with self.lock:
            previous_state = self.state_for_player(source, player_id)
            text = self._handle(source, player_id, player_name, action, room, arg)
            state = self.state_for_player(source, player_id)
        clear_room = False
        if previous_state.get("uc_active") and not state.get("uc_active"):
            previous_room = previous_state.get("uc_room", "")
            clear_room = self._room_is_finished(previous_room)
            state = {
                "uc_active": False,
                "uc_room": previous_room,
                "uc_status": "finished",
                "uc_phase": "finished",
                "uc_speaker_id": "",
                "uc_speaker_name": "",
                "uc_deadline": 0,
            }
        audience = "private"
        normalized_action = _normalize(action or "help")
        if normalized_action in {"start", "开始", "vote", "投票", "speak", "pass", "timeout", "发言", "跳过", "超时"}:
            audience = "team"
        payload = {
            "status": "success",
            "text": text,
            "undercover": True,
            "uc_audience": audience,
            "uc_clear_room": clear_room,
        }
        payload.update(state)
        return payload

    def _room_is_finished(self, room_id):
        if not room_id:
            return False
        with self._connection() as conn:
            room = self._room(conn, room_id)
            return not room or room["status"] == "finished"

    def state_for_player(self, source, player_id):
        with self.lock:
            source = _clean_text(source or "game", 16) or "game"
            player_id = _clean_text(player_id, 64)
            if not player_id:
                return {"uc_active": False}
            with self._connection() as conn:
                room = self._active_room_for(conn, source, player_id)
                if not room:
                    return {"uc_active": False}
                return {
                    "uc_active": True,
                    "uc_room": room["room_id"],
                    "uc_status": room["status"],
                    "uc_phase": room["phase"],
                    "uc_speaker_id": room["current_speaker_id"],
                    "uc_speaker_name": room["current_speaker_name"],
                    "uc_deadline": room["speech_deadline"],
                }

    def _active_room_for(self, conn, source, player_id):
        return conn.execute(
            """
            SELECT r.* FROM uc_rooms r
            JOIN uc_players p ON p.room_id = r.room_id
            WHERE p.source = ? AND p.player_id = ? AND r.status IN ('waiting', 'playing')
            ORDER BY r.updated_at DESC LIMIT 1
            """,
            (source, player_id),
        ).fetchone()

    def _room(self, conn, room_id):
        return conn.execute("SELECT * FROM uc_rooms WHERE room_id = ?", (room_id,)).fetchone()

    def _players(self, conn, room_id, alive_only=False):
        sql = "SELECT * FROM uc_players WHERE room_id = ?"
        if alive_only:
            sql += " AND alive = 1"
        sql += " ORDER BY joined_at, player_name"
        return conn.execute(sql, (room_id,)).fetchall()

    def _touch_room(self, conn, room_id):
        conn.execute("UPDATE uc_rooms SET updated_at = ? WHERE room_id = ?", (_now(), room_id))

    def create_room(self, source, player_id, player_name, room_id):
        room_id = room_id or f"{player_name}的房间"
        room_id = _clean_text(room_id, MAX_ROOM_NAME_LENGTH)
        if not room_id:
            return "用法：/uc create 房间名"
        now = _now()
        with self._connection() as conn:
            if self._active_room_for(conn, source, player_id):
                return "你已经在一个谁是卧底房间里了，先 /uc leave 或 /uc end。"
            old_room = self._room(conn, room_id)
            if old_room and old_room["status"] == "finished":
                conn.execute("DELETE FROM uc_votes WHERE room_id = ?", (room_id,))
                conn.execute("DELETE FROM uc_players WHERE room_id = ?", (room_id,))
                conn.execute("DELETE FROM uc_rooms WHERE room_id = ?", (room_id,))
            elif old_room:
                return f"房间 {room_id} 已存在，换个名字或者 /uc join {room_id}。"
            conn.execute(
                """
                INSERT INTO uc_rooms(room_id, owner_source, owner_id, owner_name, status, created_at, updated_at)
                VALUES (?, ?, ?, ?, 'waiting', ?, ?)
                """,
                (room_id, source, player_id, player_name, now, now),
            )
            conn.execute(
                """
                INSERT INTO uc_players(room_id, source, player_id, player_name, joined_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                (room_id, source, player_id, player_name, now),
            )
        return f"谁是卧底房间 {room_id} 创建成功。其他玩家用 /uc join {room_id} 加入，凑齐后房主 /uc start。"

    def join_room(self, source, player_id, player_name, room_id):
        room_id = _clean_text(room_id, MAX_ROOM_NAME_LENGTH)
        if not room_id:
            return "用法：/uc join 房间名"
        now = _now()
        with self._connection() as conn:
            if self._active_room_for(conn, source, player_id):
                return "你已经在一个谁是卧底房间里了，先 /uc leave。"
            room = self._room(conn, room_id)
            if not room or room["status"] == "finished":
                return f"没有找到等待中的房间 {room_id}。"
            if room["status"] != "waiting":
                return f"房间 {room_id} 已经开局了。"
            count = len(self._players(conn, room_id))
            if count >= MAX_PLAYERS:
                return f"房间 {room_id} 已满。"
            conn.execute(
                """
                INSERT INTO uc_players(room_id, source, player_id, player_name, turn_order, joined_at)
                VALUES (?, ?, ?, ?, ?, ?)
                """,
                (room_id, source, player_id, player_name, count, now),
            )
            self._touch_room(conn, room_id)
        return f"{player_name} 加入了谁是卧底房间 {room_id}。当前 {count + 1}/{MAX_PLAYERS} 人。"

    def leave_room(self, source, player_id, player_name):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            is_owner = room["owner_source"] == source and room["owner_id"] == player_id
            if room["status"] == "playing":
                return "游戏进行中不能离开；需要结束请让房主使用 /uc end。"
            if is_owner:
                conn.execute(
                    """
                    UPDATE uc_rooms
                    SET status = 'finished', phase = 'finished', current_speaker_source = '',
                        current_speaker_id = '', current_speaker_name = '', speech_deadline = 0,
                        updated_at = ?
                    WHERE room_id = ?
                    """,
                    (_now(), room["room_id"]),
                )
                return f"房间 {room['room_id']} 已解散。"
            conn.execute("DELETE FROM uc_votes WHERE room_id = ? AND (voter_source = ? AND voter_id = ? OR target_source = ? AND target_id = ?)",
                         (room["room_id"], source, player_id, source, player_id))
            conn.execute("DELETE FROM uc_players WHERE room_id = ? AND source = ? AND player_id = ?", (room["room_id"], source, player_id))
            self._touch_room(conn, room["room_id"])
        return f"{player_name} 已离开谁是卧底房间 {room['room_id']}。"

    def list_rooms(self):
        with self._connection() as conn:
            rows = conn.execute(
                """
                SELECT r.room_id, r.status, r.owner_name, COUNT(p.player_id) AS player_count
                FROM uc_rooms r
                LEFT JOIN uc_players p ON p.room_id = r.room_id
                WHERE r.status IN ('waiting', 'playing')
                GROUP BY r.room_id
                ORDER BY r.updated_at DESC
                LIMIT 8
                """
            ).fetchall()
        if not rows:
            return "暂无谁是卧底房间。用 /uc create 房间名 开一个。"
        lines = ["谁是卧底房间："]
        for row in rows:
            state = "等待中" if row["status"] == "waiting" else "游戏中"
            lines.append(f"{row['room_id']}：{state}，{row['player_count']} 人，房主 {row['owner_name']}")
        return "\n".join(lines)

    def players_text(self, source, player_id):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            players = self._players(conn, room["room_id"])
        names = []
        for player in players:
            suffix = "" if player["alive"] else "(出局)"
            names.append(f"{player['player_name']}{suffix}")
        return f"{room['room_id']} 玩家：{', '.join(names)}"

    def start_room(self, source, player_id):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            if room["owner_source"] != source or room["owner_id"] != player_id:
                return "只有房主可以开始谁是卧底。"
            if room["status"] != "waiting":
                return "这个房间已经开局了。"
            players = self._players(conn, room["room_id"])
            if len(players) < 3:
                return "谁是卧底至少需要 3 人。"

            civilian_word, undercover_word = random.choice(self._load_words())
            undercover_count = 1 if len(players) < 6 else 2
            shuffled = list(players)
            random.shuffle(shuffled)
            undercover_keys = {(p["source"], p["player_id"]) for p in shuffled[:undercover_count]}
            for index, player in enumerate(players):
                role = "undercover" if (player["source"], player["player_id"]) in undercover_keys else "civilian"
                word = undercover_word if role == "undercover" else civilian_word
                conn.execute(
                    """
                    UPDATE uc_players
                    SET role = ?, word = ?, alive = 1, spoken = 0, turn_order = ?
                    WHERE room_id = ? AND source = ? AND player_id = ?
                    """,
                    (role, word, index, room["room_id"], player["source"], player["player_id"]),
                )
            first_speaker = players[0]
            deadline = _now() + SPEECH_SECONDS
            conn.execute("DELETE FROM uc_votes WHERE room_id = ?", (room["room_id"],))
            conn.execute(
                """
                UPDATE uc_rooms
                SET status = 'playing', round_no = round_no + 1, civilian_word = ?,
                    undercover_word = ?, phase = 'speaking', current_speaker_source = ?,
                    current_speaker_id = ?, current_speaker_name = ?, speech_deadline = ?,
                    updated_at = ?
                WHERE room_id = ?
                """,
                (
                    civilian_word,
                    undercover_word,
                    first_speaker["source"],
                    first_speaker["player_id"],
                    first_speaker["player_name"],
                    deadline,
                    _now(),
                    room["room_id"],
                ),
            )
        return (
            f"谁是卧底 {room['room_id']} 开始！{len(players)} 人，卧底 {undercover_count} 人。\n"
            f"每个人发送 /uc word 私聊查看自己的词。第一位：{first_speaker['player_name']}，{SPEECH_SECONDS} 秒内描述。"
        )

    def word_text(self, source, player_id):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            if room["status"] != "playing":
                return "房间还没开始，等房主 /uc start。"
            player = conn.execute(
                "SELECT * FROM uc_players WHERE room_id = ? AND source = ? AND player_id = ?",
                (room["room_id"], source, player_id),
            ).fetchone()
        if not player or not player["word"]:
            return "暂时没有你的词，可能房间状态异常，请让房主重新开局。"
        if not player["alive"]:
            return f"你已出局。本局你的词是：{player['word']}"
        return f"你的词：{player['word']}。描述时别直接念词，也别暴露自己身份。"

    def status_text(self, source, player_id):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            players = self._players(conn, room["room_id"])
            alive_count = sum(1 for player in players if player["alive"])
            vote_count = conn.execute("SELECT COUNT(*) FROM uc_votes WHERE room_id = ?", (room["room_id"],)).fetchone()[0]
        state = "等待中" if room["status"] == "waiting" else "游戏中"
        phase = "发言" if room["phase"] == "speaking" else "投票" if room["phase"] == "voting" else "等待"
        extra = f"，当前发言：{room['current_speaker_name']}" if room["phase"] == "speaking" and room["current_speaker_name"] else ""
        return f"{room['room_id']}：{state}/{phase}，第 {room['round_no']} 局，存活 {alive_count}/{len(players)}，已投票 {vote_count}{extra}。"

    def _next_speaker(self, conn, room_id):
        return conn.execute(
            """
            SELECT * FROM uc_players
            WHERE room_id = ? AND alive = 1 AND spoken = 0
            ORDER BY turn_order, joined_at
            LIMIT 1
            """,
            (room_id,),
        ).fetchone()

    def _start_speech_round(self, conn, room_id):
        conn.execute("UPDATE uc_players SET spoken = 0 WHERE room_id = ? AND alive = 1", (room_id,))
        speaker = self._next_speaker(conn, room_id)
        if not speaker:
            return None
        deadline = _now() + SPEECH_SECONDS
        conn.execute(
            """
            UPDATE uc_rooms
            SET phase = 'speaking', current_speaker_source = ?, current_speaker_id = ?,
                current_speaker_name = ?, speech_deadline = ?, updated_at = ?
            WHERE room_id = ?
            """,
            (speaker["source"], speaker["player_id"], speaker["player_name"], deadline, _now(), room_id),
        )
        return speaker

    def _advance_speaker_or_vote(self, conn, room_id):
        speaker = self._next_speaker(conn, room_id)
        if speaker:
            deadline = _now() + SPEECH_SECONDS
            conn.execute(
                """
                UPDATE uc_rooms
                SET phase = 'speaking', current_speaker_source = ?, current_speaker_id = ?,
                    current_speaker_name = ?, speech_deadline = ?, updated_at = ?
                WHERE room_id = ?
                """,
                (speaker["source"], speaker["player_id"], speaker["player_name"], deadline, _now(), room_id),
            )
            return f"下一位发言：{speaker['player_name']}，{SPEECH_SECONDS} 秒。"
        conn.execute(
            """
            UPDATE uc_rooms
            SET phase = 'voting', current_speaker_source = '', current_speaker_id = '',
                current_speaker_name = '', speech_deadline = 0, updated_at = ?
            WHERE room_id = ?
            """,
            (_now(), room_id),
        )
        return "本轮发言结束，进入投票。所有存活玩家使用 /uc vote 玩家名。"

    def speak(self, source, player_id, player_name, action):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            if room["status"] != "playing" or room["phase"] != "speaking":
                return "现在不是发言阶段。"
            is_timeout = _normalize(action) in {"timeout", "超时"}
            if is_timeout and room["speech_deadline"] > _now():
                return f"还没超时。当前发言者是 {room['current_speaker_name']}。"
            if not is_timeout and (room["current_speaker_source"] != source or room["current_speaker_id"] != player_id):
                return f"还没轮到你发言。当前发言者是 {room['current_speaker_name']}。"
            if room["current_speaker_id"]:
                conn.execute(
                    """
                    UPDATE uc_players SET spoken = 1
                    WHERE room_id = ? AND source = ? AND player_id = ?
                    """,
                    (room["room_id"], room["current_speaker_source"], room["current_speaker_id"]),
                )
            next_text = self._advance_speaker_or_vote(conn, room["room_id"])
        if is_timeout:
            return f"{room['current_speaker_name']} 发言超时。\n{next_text}"
        if _normalize(action) in {"pass", "跳过"}:
            return f"{player_name} 跳过了本轮发言。\n{next_text}"
        return f"{player_name} 发言结束。\n{next_text}"

    def _match_alive_player(self, players, query):
        needle = _normalize(query)
        if not needle:
            return None, "用法：/uc vote 玩家名"
        alive = [player for player in players if player["alive"]]
        exact = [player for player in alive if _normalize(player["player_name"]) == needle]
        if len(exact) == 1:
            return exact[0], ""
        partial = [player for player in alive if needle in _normalize(player["player_name"])]
        if len(partial) == 1:
            return partial[0], ""
        if len(partial) > 1:
            return None, "这个名字匹配到多人，请输入更完整的玩家名。"
        return None, "没有找到这个存活玩家。"

    def vote(self, source, player_id, player_name, target_name):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            if room["status"] != "playing":
                return "房间还没开始。"
            if room["phase"] != "voting":
                speaker = room["current_speaker_name"] or "当前玩家"
                return f"还在发言阶段，轮到 {speaker}。所有人发言完再投票。"
            voter = conn.execute(
                "SELECT * FROM uc_players WHERE room_id = ? AND source = ? AND player_id = ?",
                (room["room_id"], source, player_id),
            ).fetchone()
            if not voter or not voter["alive"]:
                return "你已经出局，不能投票。"
            players = self._players(conn, room["room_id"])
            target, error = self._match_alive_player(players, target_name)
            if error:
                return error
            if target["source"] == source and target["player_id"] == player_id:
                return "不能投自己。"
            conn.execute(
                """
                INSERT INTO uc_votes(room_id, voter_source, voter_id, target_source, target_id)
                VALUES (?, ?, ?, ?, ?)
                ON CONFLICT(room_id, voter_source, voter_id)
                DO UPDATE SET target_source = excluded.target_source, target_id = excluded.target_id
                """,
                (room["room_id"], source, player_id, target["source"], target["player_id"]),
            )
            alive_players = self._players(conn, room["room_id"], alive_only=True)
            votes = conn.execute("SELECT * FROM uc_votes WHERE room_id = ?", (room["room_id"],)).fetchall()
            if len(votes) < len(alive_players):
                self._touch_room(conn, room["room_id"])
                return f"{player_name} 已投票给 {target['player_name']}。当前 {len(votes)}/{len(alive_players)} 票。"

            counts = Counter((vote["target_source"], vote["target_id"]) for vote in votes)
            max_votes = max(counts.values())
            winners = [key for key, count in counts.items() if count == max_votes]
            if len(winners) != 1:
                conn.execute("DELETE FROM uc_votes WHERE room_id = ?", (room["room_id"],))
                self._touch_room(conn, room["room_id"])
                return "本轮平票，无人出局。请重新 /uc vote 玩家名。"

            out_source, out_id = winners[0]
            eliminated = next(player for player in alive_players if player["source"] == out_source and player["player_id"] == out_id)
            conn.execute(
                "UPDATE uc_players SET alive = 0 WHERE room_id = ? AND source = ? AND player_id = ?",
                (room["room_id"], out_source, out_id),
            )
            conn.execute("DELETE FROM uc_votes WHERE room_id = ?", (room["room_id"],))
            after_players = self._players(conn, room["room_id"], alive_only=True)
            undercover_alive = [player for player in after_players if player["role"] == "undercover"]
            civilian_alive = [player for player in after_players if player["role"] == "civilian"]
            if not undercover_alive:
                return self._finish(conn, room, "civilian", f"{eliminated['player_name']} 出局，他是卧底。平民胜利！")
            if len(undercover_alive) >= len(civilian_alive):
                return self._finish(conn, room, "undercover", f"{eliminated['player_name']} 出局。卧底人数已追平，卧底胜利！")
            next_speaker = self._start_speech_round(conn, room["room_id"])
            if not next_speaker:
                return self._finish(conn, room, "civilian", f"{eliminated['player_name']} 出局。房间状态已收尾，平民胜利！")
        return f"{eliminated['player_name']} 出局。进入下一轮发言，存活 {len(after_players)} 人。第一位：{next_speaker['player_name']}，{SPEECH_SECONDS} 秒。"

    def _finish(self, conn, room, winner_role, headline):
        players = self._players(conn, room["room_id"])
        now = _now()
        for player in players:
            won = player["role"] == winner_role
            conn.execute(
                """
                INSERT INTO uc_stats(source, player_id, player_name, games, wins, undercover_wins, civilian_wins, updated_at)
                VALUES (?, ?, ?, 1, ?, ?, ?, ?)
                ON CONFLICT(source, player_id) DO UPDATE SET
                    player_name = excluded.player_name,
                    games = games + 1,
                    wins = wins + excluded.wins,
                    undercover_wins = undercover_wins + excluded.undercover_wins,
                    civilian_wins = civilian_wins + excluded.civilian_wins,
                    updated_at = excluded.updated_at
                """,
                (
                    player["source"],
                    player["player_id"],
                    player["player_name"],
                    1 if won else 0,
                    1 if won and winner_role == "undercover" else 0,
                    1 if won and winner_role == "civilian" else 0,
                    now,
                ),
            )
        conn.execute(
            """
            UPDATE uc_rooms
            SET status = 'finished', phase = 'finished', current_speaker_source = '',
                current_speaker_id = '', current_speaker_name = '', speech_deadline = 0,
                updated_at = ?
            WHERE room_id = ?
            """,
            (now, room["room_id"]),
        )
        undercover_names = [player["player_name"] for player in players if player["role"] == "undercover"]
        return (
            f"{headline}\n"
            f"平民词：{room['civilian_word']}；卧底词：{room['undercover_word']}\n"
            f"卧底：{', '.join(undercover_names)}"
        )

    def end_room(self, source, player_id):
        with self._connection() as conn:
            room = self._active_room_for(conn, source, player_id)
            if not room:
                return "你不在谁是卧底房间里。"
            if room["owner_source"] != source or room["owner_id"] != player_id:
                return "只有房主可以结束房间。"
            conn.execute(
                """
                UPDATE uc_rooms
                SET status = 'finished', phase = 'finished', current_speaker_source = '',
                    current_speaker_id = '', current_speaker_name = '', speech_deadline = 0,
                    updated_at = ?
                WHERE room_id = ?
                """,
                (_now(), room["room_id"]),
            )
        return f"谁是卧底房间 {room['room_id']} 已结束。"

    def rank_text(self, limit=10):
        with self._connection() as conn:
            rows = conn.execute(
                """
                SELECT player_name, games, wins, undercover_wins, civilian_wins
                FROM uc_stats
                ORDER BY wins DESC, games DESC, updated_at DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()
        if not rows:
            return "谁是卧底排行榜暂无记录。"
        lines = ["谁是卧底排行榜："]
        for index, row in enumerate(rows, start=1):
            lines.append(
                f"{index}. {row['player_name']} - {row['wins']} 胜/{row['games']} 局 "
                f"(卧底 {row['undercover_wins']}，平民 {row['civilian_wins']})"
            )
        return "\n".join(lines)
