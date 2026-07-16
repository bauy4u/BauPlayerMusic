import tempfile
import unittest
from pathlib import Path

from qqbot_bridge import QQBotConfig
from music_stats import MusicStatsStore, format_guess_rank, format_music_rank, format_personal_rank


class MusicStatsStoreTests(unittest.TestCase):
    def test_records_and_ranks_requests(self):
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "music.sqlite3"
            store = MusicStatsStore(database)

            store.record("1", "Song A", "Artist", "game", "Alice", "Alice", 1700000000)
            store.record("1", "Song A", "Artist", "game", "Alice", "Alice", 1700000001)
            store.record("2", "Song B", "Artist", "qq", "10001", "Bob", 1700000002)

            summary = store.summary()
            personal = store.personal("game", "Alice")

            self.assertEqual(summary["total"], 3)
            self.assertEqual(summary["top_requesters"][0]["requester_name"], "Alice")
            self.assertEqual(summary["top_songs"][0]["title"], "Song A")
            self.assertEqual(personal["total"], 2)
            self.assertIn("Alice", format_music_rank(summary))
            self.assertIn("累计点歌 2 次", format_personal_rank("Alice", personal))

            database.unlink()

    def test_backend_history_state_and_rank_endpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            import mds

            original_store = mds.MUSIC_STATS
            original_token = mds.QQBOT_BRIDGE.config
            original_state = mds.server_state_snapshot()
            database = Path(directory) / "api.sqlite3"
            mds.MUSIC_STATS = MusicStatsStore(database)
            try:
                client = mds.app.test_client()
                record = client.post(
                    "/history/record",
                    json={
                        "song_id": "123",
                        "title": "Song",
                        "artist": "Artist",
                        "requester_source": "game",
                        "requester_id": "Alice",
                        "requester_name": "Alice",
                    },
                )
                state = client.post(
                    "/server/state",
                    json={"map_name": "ctf7", "players": ["Alice", "Bob\nHidden"]},
                )
                rank = client.get("/stats/musicrank")
                personal = client.get("/stats/myrank?source=game&id=Alice&name=Alice")
                status = client.get("/identity/status?game_name=Alice")

                self.assertEqual(record.status_code, 200)
                self.assertEqual(state.status_code, 200)
                self.assertEqual(rank.get_json()["total"], 1)
                self.assertEqual(personal.get_json()["total"], 1)
                self.assertFalse(status.get_json()["bound"])
                self.assertIn("在线：2 人", mds.format_server_status())
                self.assertIn("Bob Hidden", mds.format_server_players())
            finally:
                mds.MUSIC_STATS = original_store
                mds.QQBOT_BRIDGE.config = original_token
                with mds.SERVER_STATE_LOCK:
                    mds.SERVER_STATE.update(original_state)

    def test_identity_binding_and_unified_personal_stats(self):
        with tempfile.TemporaryDirectory() as directory:
            store = MusicStatsStore(Path(directory) / "identity.sqlite3")
            code = store.create_bind_code("Alice", timestamp=1700000000)
            result = store.consume_bind_code(
                code["code"], "10001", "QQ Alice", timestamp=1700000001
            )
            self.assertTrue(result["success"])
            self.assertEqual(store.binding_for_qq("10001")["game_name"], "Alice")

            store.record("1", "Song A", "Artist", "game", "Alice", "Alice", 1700000010)
            store.record("2", "Song B", "Artist", "qq", "10001", "QQ Alice", 1700000011)
            self.assertEqual(store.personal("qq", "10001")["total"], 2)
            self.assertEqual(store.personal("game", "alice")["total"], 2)
            summary = store.summary()
            self.assertEqual(summary["top_requesters"][0]["requester_name"], "Alice")
            self.assertEqual(summary["top_requesters"][0]["count"], 2)

            conflict_code = store.create_bind_code("Alice", timestamp=1700000020)
            conflict = store.consume_bind_code(
                conflict_code["code"], "20002", "Other", timestamp=1700000021
            )
            self.assertFalse(conflict["success"])
            self.assertIn("其他 QQ", conflict["message"])
            unbound = store.unbind_qq("10001")
            self.assertTrue(unbound["success"])
            self.assertIsNone(store.binding_for_qq("10001"))

    def test_expired_identity_code_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            store = MusicStatsStore(Path(directory) / "identity.sqlite3")
            code = store.create_bind_code("Alice", ttl_seconds=60, timestamp=1700000000)
            result = store.consume_bind_code(
                code["code"], "10001", "QQ Alice", timestamp=1700000061
            )
            self.assertFalse(result["success"])

    def test_backend_guess_game_round_and_rank(self):
        with tempfile.TemporaryDirectory() as directory:
            import mds

            playlist = Path(directory) / "playlist.txt"
            playlist.write_text(
                "STATE|0|100|180.00|true|1\n"
                "Bad Apple|Nomico|song-1|180.00|true|true|Alice|game|Alice\n",
                encoding="utf-8",
            )
            original_store = mds.MUSIC_STATS
            original_config = mds.QQBOT_BRIDGE.config
            original_round = dict(mds.GUESS_ROUND)
            mds.MUSIC_STATS = MusicStatsStore(Path(directory) / "guess.sqlite3")
            mds.QQBOT_BRIDGE.config = QQBotConfig(
                enabled=False,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset(),
                playlist_file=playlist,
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            try:
                client = mds.app.test_client()
                prompt = client.get("/guess?source=game&id=Alice&name=Alice").get_json()["text"]
                wrong = client.get("/guess?source=game&id=Alice&name=Alice&answer=wrong").get_json()["text"]
                right = client.get("/guess?source=game&id=Alice&name=Alice&answer=bad%20apple").get_json()["text"]
                duplicate = client.get("/guess?source=qq&id=10001&name=Bob&answer=bad%20apple").get_json()["text"]
                rank = client.get("/guessrank").get_json()["text"]

                self.assertIn("猜当前歌", prompt)
                self.assertIn("没猜中", wrong)
                self.assertIn("猜中了", right)
                self.assertIn("已经被 Alice", duplicate)
                self.assertIn("Alice - 1 次", rank)
                self.assertIn("Alice - 1 次", format_guess_rank(mds.MUSIC_STATS.guess_rank()))
            finally:
                mds.MUSIC_STATS = original_store
                mds.QQBOT_BRIDGE.config = original_config
                with mds.GUESS_LOCK:
                    mds.GUESS_ROUND.update(original_round)


if __name__ == "__main__":
    unittest.main()
