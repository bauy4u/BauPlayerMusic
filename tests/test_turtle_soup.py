import tempfile
import unittest
from pathlib import Path

from turtle_soup import TurtleSoupStore


class TurtleSoupStoreTests(unittest.TestCase):
    def test_start_and_answer_without_api_key(self):
        with tempfile.TemporaryDirectory() as directory:
            store = TurtleSoupStore(Path(directory) / "turtle.sqlite3", api_key="")
            start = store.handle("game", "Alice", "Alice", "开始游戏")
            self.assertEqual(start["status"], "success")
            self.assertIn("新案子", start["text"])

            store.player_cooldown = 0
            store.global_cooldown = 0
            answer = store.handle("game", "Bob", "Bob", "这是魔术相关吗")
            self.assertEqual(answer["status"], "success")
            self.assertRegex(answer["text"], r"Bob: (是|否|不重要|是也不是|无法判断)")

    def test_player_cooldown(self):
        with tempfile.TemporaryDirectory() as directory:
            store = TurtleSoupStore(Path(directory) / "turtle.sqlite3", api_key="")
            store.handle("game", "Alice", "Alice", "开始游戏")
            second = store.handle("game", "Alice", "Alice", "这是魔术吗")
            self.assertIn("秒后再问", second["text"])

    def test_reveal_bypasses_cooldown_and_ends_game(self):
        with tempfile.TemporaryDirectory() as directory:
            store = TurtleSoupStore(Path(directory) / "turtle.sqlite3", api_key="")
            store.handle("game", "Alice", "Alice", "开始游戏")

            reveal = store.handle("game", "Alice", "Alice", "揭晓答案")
            status = store.handle("game", "Bob", "Bob", "答案")

            self.assertIn("真相是", reveal["text"])
            self.assertIn("还没有案子", status["text"])

    def test_solution_guess_wins_and_reveals_truth(self):
        with tempfile.TemporaryDirectory() as directory:
            store = TurtleSoupStore(Path(directory) / "turtle.sqlite3", api_key="")
            store.player_cooldown = 0
            store.global_cooldown = 0
            store.handle("game", "Alice", "Alice", "开始游戏")
            case = store._current_case()

            result = store.handle("game", "Bob", "Bob", case.truth)
            status = store.handle("game", "Cindy", "Cindy", "答案")

            self.assertIn("Bob: 你赢了", result["text"])
            self.assertIn("真相是", result["text"])
            self.assertIn("还没有案子", status["text"])

    def test_memory_database_keeps_schema_across_calls(self):
        store = TurtleSoupStore(":memory:", api_key="")
        start = store.handle("game", "Alice", "Alice", "开始游戏")
        reveal = store.handle("game", "Alice", "Alice", "答案")

        self.assertEqual(start["status"], "success")
        self.assertIn("真相是", reveal["text"])


if __name__ == "__main__":
    unittest.main()
