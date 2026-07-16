import json
import tempfile
import time
import unittest
from pathlib import Path

from qqbot_bridge import QQBotBridge, QQBotConfig, daily_luck_text, format_playlist_message, read_playlist_snapshot


class FakeWebSocket:
    def __init__(self):
        self.messages = []

    def send(self, message):
        self.messages.append(json.loads(message))


class QQBotBridgeTests(unittest.TestCase):
    def test_reads_and_formats_playlist(self):
        with tempfile.TemporaryDirectory() as directory:
            playlist = Path(directory) / "playlist.txt"
            playlist.write_text(
                "STATE|0|100|185.17|true|1\n"
                "Song\\|Title|Artist|123|185.17|true|true|Alice|game|Alice\n"
                "Next Song|Next Artist|456|120.00|false|false|Bob|game|Bob\n",
                encoding="utf-8",
            )

            snapshot = read_playlist_snapshot(playlist)
            message = format_playlist_message(snapshot, now=130)

            self.assertEqual(snapshot["songs"][0]["title"], "Song|Title")
            self.assertEqual(snapshot["songs"][0]["requester_name"], "Alice")
            self.assertIn("00:30/03:05", message)
            self.assertIn("2. Next Song - Next Artist", message)
            self.assertIn("点歌: Bob", message)

    def test_backend_queue_endpoint_uses_authoritative_file(self):
        with tempfile.TemporaryDirectory() as directory:
            playlist = Path(directory) / "playlist.txt"
            playlist.write_text(
                "STATE|0|100|185.17|true|1\n"
                "Current Song|Artist|123|185.17|true|true\n",
                encoding="utf-8",
            )

            import mds

            original_config = mds.QQBOT_BRIDGE.config
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
                response = mds.app.test_client().get("/queue")
                payload = response.get_json()
            finally:
                mds.QQBOT_BRIDGE.config = original_config

            self.assertEqual(response.status_code, 200)
            self.assertTrue(payload["is_playing"])
            self.assertEqual(payload["songs"][0]["title"], "Current Song")

    def test_only_replies_to_allowed_group_when_mentioned(self):
        with tempfile.TemporaryDirectory() as directory:
            playlist = Path(directory) / "playlist.txt"
            playlist.write_text("STATE|-1|0|0.00|false|0\n", encoding="utf-8")
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=playlist,
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "message_id": 88,
                "message": [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": " /mls"}},
                ],
            }

            bridge._on_message(websocket, json.dumps(event))

            self.assertEqual(len(websocket.messages), 1)
            self.assertEqual(websocket.messages[0]["action"], "send_group_msg")
            self.assertEqual(websocket.messages[0]["params"]["group_id"], "12345")
            self.assertIn("播放队列为空", websocket.messages[0]["params"]["message"][1]["data"]["text"])

            event["group_id"] = 99999
            bridge._on_message(websocket, json.dumps(event))
            event["group_id"] = 12345
            event["message"] = [{"type": "text", "data": {"text": "/mls"}}]
            bridge._on_message(websocket, json.dumps(event))
            self.assertEqual(len(websocket.messages), 1)

    def test_group_say_is_queued_and_cooldown_is_enforced(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="secret",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "message_id": 99,
                "sender": {"card": "群友甲", "nickname": "甲"},
                "message": [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": " /say 大家好"}},
                ],
            }

            bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))
            queued = bridge.poll_server_messages()
            self.assertEqual(len(queued), 1)
            self.assertIn("群友甲", queued[0]["text"])
            self.assertIn("大家好", queued[0]["text"])

            event["message_id"] = 100
            bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))
            self.assertEqual(bridge.poll_server_messages(), [])
            self.assertIn("冷却", websocket.messages[-1]["params"]["message"][-1]["data"]["text"])

    def test_backend_send_endpoint_uses_token_and_onebot(self):
        import mds

        config = QQBotConfig(
            enabled=True,
            websocket_url="ws://127.0.0.1:3001",
            access_token="",
            group_ids=frozenset({12345}),
            playlist_file=Path("playlist.txt"),
            max_songs=10,
            reconnect_seconds=5,
            server_token="server-secret",
            relay_global_cooldown=5,
            relay_user_cooldown=30,
            relay_max_length=160,
        )
        bridge = QQBotBridge(config)
        bridge._connected = True
        bridge._websocket = FakeWebSocket()
        original_bridge = mds.QQBOT_BRIDGE
        mds.QQBOT_BRIDGE = bridge
        try:
            client = mds.app.test_client()
            denied = client.post("/qqbot/send", json={"player_name": "Player", "message": "Hello"})
            response = client.post(
                "/qqbot/send",
                headers={"Authorization": "Bearer server-secret"},
                json={"player_name": "Player", "message": "Hello"},
            )
        finally:
            mds.QQBOT_BRIDGE = original_bridge

        self.assertEqual(denied.status_code, 401)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(bridge._websocket.messages), 1)
        sent_text = bridge._websocket.messages[0]["params"]["message"][0]["data"]["text"]
        self.assertIn("Player", sent_text)
        self.assertIn("Hello", sent_text)

    def test_status_players_and_rank_commands_use_providers(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            bridge.set_command_providers(
                lambda: "status-result",
                lambda: "players-result",
                lambda: "rank-result",
                lambda source, requester_id, name: f"{source}:{requester_id}:{name}",
            )
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "sender": {"card": "群友甲", "nickname": "甲"},
            }

            for index, command in enumerate(("/status", "/players", "/musicrank", "/myrank"), start=1):
                event["message_id"] = 200 + index
                event["message"] = [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": f" {command}"}},
                ]
                bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))

            replies = [
                message["params"]["message"][-1]["data"]["text"]
                for message in websocket.messages
            ]
            self.assertEqual(replies[:3], ["status-result", "players-result", "rank-result"])
            self.assertEqual(replies[3], "qq:20001:群友甲")

    def test_bind_profile_and_ddnet_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "profile.png"
            image.write_bytes(b"png-test")
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            bridge.set_command_providers(
                lambda: "",
                lambda: "",
                lambda: "",
                lambda *_: "",
                lambda qq_id, name, code: f"bound:{qq_id}:{name}:{code}",
                lambda qq_id: f"profile:{qq_id}",
                lambda qq_id, player="": {"text": f"ddnet:{qq_id}:{player}", "image_path": image},
            )
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "sender": {"card": "群友甲"},
            }
            for message_id, command in ((301, "/bind ABCD2345"), (302, "/me"), (303, "/ddnet bau")):
                event["message_id"] = message_id
                event["message"] = [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": f" {command}"}},
                ]
                bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))

            deadline = time.time() + 2
            while len(websocket.messages) < 3 and time.time() < deadline:
                time.sleep(0.01)
            replies = {
                message["echo"]: message["params"]["message"]
                for message in websocket.messages
            }
            self.assertIn("bound:20001:群友甲:ABCD2345", replies["bpmusic-mls-301"][-1]["data"]["text"])
            self.assertEqual(replies["bpmusic-mls-302"][-1]["data"]["text"], "profile:20001")
            self.assertEqual(replies["bpmusic-mls-303"][-2]["data"]["text"], "ddnet:20001:bau")
            self.assertTrue(replies["bpmusic-mls-303"][-1]["data"]["file"].startswith("base64://"))

    def test_help_unbind_and_ddnet_cooldown(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            bridge.set_command_providers(
                lambda: "", lambda: "", lambda: "", lambda *_: "",
                ddnet_profile=lambda qq_id, player="": f"ddnet:{player}",
                unbind_identity=lambda qq_id: f"unbound:{qq_id}",
            )
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "sender": {"card": "群友甲"},
            }
            for message_id, command in ((501, "/help"), (502, "/unbind"), (503, "/ddnet bau"), (504, "/ddnet other")):
                event["message_id"] = message_id
                event["message"] = [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": f" {command}"}},
                ]
                bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))

            deadline = time.time() + 2
            while len(websocket.messages) < 4 and time.time() < deadline:
                time.sleep(0.01)
            replies = {
                message["echo"]: message["params"]["message"][-1]["data"]["text"]
                for message in websocket.messages
            }
            self.assertIn("/ddnet [玩家名]", replies["bpmusic-mls-501"])
            self.assertEqual(replies["bpmusic-mls-502"], "unbound:20001")
            self.assertEqual(replies["bpmusic-mls-503"], "ddnet:bau")
            self.assertIn("冷却", replies["bpmusic-mls-504"])

    def test_fun_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "sender": {"card": "Alice"},
            }
            for message_id, command in ((601, "/jrrp"), (602, "/roll 2d6"), (603, "/pick 红茶 | 咖啡"), (604, "/help")):
                event["message_id"] = message_id
                event["message"] = [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": f" {command}"}},
                ]
                bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))

            replies = {
                message["echo"]: message["params"]["message"][-1]["data"]["text"]
                for message in websocket.messages
            }
            self.assertEqual(
                replies["bpmusic-mls-601"],
                daily_luck_text("Alice", "qq:20001"),
            )
            self.assertRegex(replies["bpmusic-mls-602"], r"掷出 2d6：\d+ \+ \d+ = \d+")
            self.assertIn(replies["bpmusic-mls-603"], {"我选：红茶", "我选：咖啡"})
            self.assertIn("/jrrp", replies["bpmusic-mls-604"])
            self.assertIn("/roll", replies["bpmusic-mls-604"])
            self.assertIn("/pick", replies["bpmusic-mls-604"])

    def test_guess_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=5,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            bridge.set_command_providers(
                lambda: "", lambda: "", lambda: "", lambda *_: "",
                guess_game=lambda source, user_id, name, answer="": f"guess:{source}:{user_id}:{name}:{answer}",
                guess_rank=lambda: "guess-rank",
            )
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "sender": {"card": "Alice"},
            }
            for message_id, command in ((701, "/guess"), (702, "/guess Bad Apple"), (703, "/guessrank")):
                event["message_id"] = message_id
                event["message"] = [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": f" {command}"}},
                ]
                bridge._on_message(websocket, json.dumps(event, ensure_ascii=False))

            replies = {
                message["echo"]: message["params"]["message"][-1]["data"]["text"]
                for message in websocket.messages
            }
            self.assertEqual(replies["bpmusic-mls-701"], "guess:qq:20001:Alice:")
            self.assertEqual(replies["bpmusic-mls-702"], "guess:qq:20001:Alice:Bad Apple")
            self.assertEqual(replies["bpmusic-mls-703"], "guess-rank")

    def test_bound_identity_is_used_for_relay_name_and_shared_cooldown(self):
        with tempfile.TemporaryDirectory() as directory:
            config = QQBotConfig(
                enabled=True,
                websocket_url="ws://127.0.0.1:3001",
                access_token="",
                group_ids=frozenset({12345}),
                playlist_file=Path(directory) / "playlist.txt",
                max_songs=10,
                reconnect_seconds=5,
                server_token="",
                relay_global_cooldown=0,
                relay_user_cooldown=30,
                relay_max_length=160,
            )
            bridge = QQBotBridge(config)
            bridge.set_command_providers(
                lambda: "", lambda: "", lambda: "", lambda *_: "",
                identity_context=lambda source, identity_id, name: {
                    "key": "bound:20001",
                    "display_name": "Alice",
                    "role": "user",
                },
            )
            websocket = FakeWebSocket()
            event = {
                "post_type": "message",
                "message_type": "group",
                "group_id": 12345,
                "user_id": 20001,
                "self_id": 10001,
                "message_id": 400,
                "sender": {"card": "QQ Alice"},
                "message": [
                    {"type": "at", "data": {"qq": "10001"}},
                    {"type": "text", "data": {"text": " /say hello"}},
                ],
            }
            bridge._on_message(websocket, json.dumps(event))
            self.assertIn("Alice", bridge.poll_server_messages()[0]["text"])

            bridge._connected = True
            bridge._websocket = websocket
            success, message = bridge.send_server_message("Alice", "back")
            self.assertFalse(success)
            self.assertIn("冷却", message)


if __name__ == "__main__":
    unittest.main()
