import sqlite3
import tempfile
import unittest
from pathlib import Path

from undercover_game import UndercoverStore


class UndercoverStoreTests(unittest.TestCase):
    def test_room_persists_and_finishes_by_vote(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "undercover.sqlite3"
            words = Path(directory) / "words.txt"
            words.write_text("苹果|梨\n", encoding="utf-8")

            store = UndercoverStore(database, words)
            self.assertIn("创建成功", store.handle("game", "Alice", "Alice", "create", "room1"))
            self.assertIn("加入", store.handle("game", "Bob", "Bob", "join", "room1"))
            self.assertIn("加入", store.handle("game", "Cindy", "Cindy", "join", "room1"))

            restored = UndercoverStore(database, words)
            self.assertIn("room1", restored.handle("game", "Alice", "Alice", "status"))
            self.assertIn("开始", restored.handle("game", "Alice", "Alice", "start"))
            self.assertIn("你的词", restored.handle("game", "Bob", "Bob", "word"))
            for _ in range(3):
                conn = sqlite3.connect(database)
                try:
                    conn.row_factory = sqlite3.Row
                    speaker = conn.execute(
                        "SELECT current_speaker_id FROM uc_rooms WHERE room_id = 'room1'"
                    ).fetchone()["current_speaker_id"]
                finally:
                    conn.close()
                restored.handle("game", speaker, speaker, "speak")

            conn = sqlite3.connect(database)
            try:
                conn.row_factory = sqlite3.Row
                undercover = conn.execute(
                    "SELECT player_name FROM uc_players WHERE room_id = 'room1' AND role = 'undercover'"
                ).fetchone()["player_name"]
            finally:
                conn.close()

            fallback_target = "Alice" if undercover != "Alice" else "Bob"
            for name in ["Alice", "Bob", "Cindy"]:
                target = fallback_target if name == undercover else undercover
                text = restored.handle("game", name, name, "vote", arg=target)
            self.assertIn("平民胜利", text)
            self.assertIn("卧底", text)
            self.assertIn("谁是卧底排行榜", restored.handle("game", "Alice", "Alice", "rank"))

    def test_backend_undercover_endpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            import mds

            original_undercover = mds.UNDERCOVER
            mds.UNDERCOVER = UndercoverStore(
                Path(directory) / "api_undercover.sqlite3",
                Path(directory) / "api_words.txt",
            )
            try:
                client = mds.app.test_client()
                create = client.get("/undercover?source=game&id=Alice&name=Alice&action=create&room=apiroom")
                rooms = client.get("/undercover?source=game&id=Alice&name=Alice&action=list")

                self.assertEqual(create.status_code, 200)
                self.assertIn("创建成功", create.get_json()["text"])
                self.assertIn("apiroom", rooms.get_json()["text"])
            finally:
                mds.UNDERCOVER = original_undercover

    def test_inconsistent_migrated_room_is_reusable(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "stale.sqlite3"
            words = Path(directory) / "words.txt"
            store = UndercoverStore(database, words)
            self.assertIn("创建成功", store.handle("game", "Alice", "Alice", "create", "room1"))
            conn = sqlite3.connect(database)
            try:
                conn.execute(
                    "UPDATE uc_rooms SET status = 'playing', phase = 'waiting' WHERE room_id = 'room1'"
                )
                conn.commit()
            finally:
                conn.close()

            repaired = UndercoverStore(database, words)
            self.assertIn("创建成功", repaired.handle("game", "Alice", "Alice", "create", "room1"))
            self.assertIn("加入", repaired.handle("game", "Bob", "Bob", "join", "room1"))

    def test_leave_payload_only_clears_leaving_player(self):
        with tempfile.TemporaryDirectory() as directory:
            store = UndercoverStore(Path(directory) / "leave.sqlite3", Path(directory) / "words.txt")
            store.handle("game", "Alice", "Alice", "create", "room1")
            store.handle("game", "Bob", "Bob", "join", "room1")

            leave = store.payload("game", "Bob", "Bob", "leave")
            alice_status = store.handle("game", "Alice", "Alice", "status")

            self.assertFalse(leave["uc_active"])
            self.assertFalse(leave["uc_clear_room"])
            self.assertIn("room1", alice_status)

    def test_playing_room_rejects_leave(self):
        with tempfile.TemporaryDirectory() as directory:
            words = Path(directory) / "words.txt"
            words.write_text("苹果|梨\n", encoding="utf-8")
            store = UndercoverStore(Path(directory) / "playing_leave.sqlite3", words)
            store.handle("game", "Alice", "Alice", "create", "room1")
            store.handle("game", "Bob", "Bob", "join", "room1")
            store.handle("game", "Cindy", "Cindy", "join", "room1")
            store.handle("game", "Alice", "Alice", "start")

            leave = store.payload("game", "Bob", "Bob", "leave")

            self.assertTrue(leave["uc_active"])
            self.assertIn("不能离开", leave["text"])

    def test_timeout_cannot_be_triggered_before_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            words = Path(directory) / "words.txt"
            words.write_text("苹果|梨\n", encoding="utf-8")
            store = UndercoverStore(Path(directory) / "early_timeout.sqlite3", words)
            store.handle("game", "Alice", "Alice", "create", "room1")
            store.handle("game", "Bob", "Bob", "join", "room1")
            store.handle("game", "Cindy", "Cindy", "join", "room1")
            store.handle("game", "Alice", "Alice", "start")

            text = store.handle("game", "Bob", "Bob", "timeout")
            status = store.handle("game", "Alice", "Alice", "status")

            self.assertIn("还没超时", text)
            self.assertIn("当前发言", status)

    def test_memory_database_keeps_schema_across_calls(self):
        store = UndercoverStore(":memory:")
        self.assertIn("创建成功", store.handle("game", "Alice", "Alice", "create", "room1"))
        self.assertIn("room1", store.handle("game", "Alice", "Alice", "status"))


if __name__ == "__main__":
    unittest.main()
