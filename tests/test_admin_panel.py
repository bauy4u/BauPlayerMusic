import tempfile
import unittest
from pathlib import Path

from admin_panel import EnvDocument, ServerConfigDocument


class EnvDocumentTests(unittest.TestCase):
    def test_update_preserves_comments_and_unknown_values(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example = root / ".env.example"
            target = root / ".env"
            example.write_text(
                "# QQ 配置说明\n"
                "BPMUSIC_QQBOT_ENABLED=0\n"
                "BPMUSIC_QQBOT_ACCESS_TOKEN=old-secret\n"
                "UNRELATED_VALUE=keep-me\n",
                encoding="utf-8",
            )
            document = EnvDocument(target, example)
            document.update({"BPMUSIC_QQBOT_ENABLED": "1", "BPMUSIC_QQBOT_ACCESS_TOKEN": ""})
            text = target.read_text(encoding="utf-8")
            self.assertIn("# QQ 配置说明", text)
            self.assertIn("BPMUSIC_QQBOT_ENABLED=1", text)
            self.assertIn("BPMUSIC_QQBOT_ACCESS_TOKEN=old-secret", text)
            self.assertIn("UNRELATED_VALUE=keep-me", text)

    def test_secret_can_be_explicitly_cleared(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example = root / ".env.example"
            target = root / ".env"
            example.write_text("BPMUSIC_S3_SECRET_KEY=secret\n", encoding="utf-8")
            document = EnvDocument(target, example)
            document.update({"BPMUSIC_S3_SECRET_KEY": ""}, {"BPMUSIC_S3_SECRET_KEY"})
            self.assertEqual(document.read()["BPMUSIC_S3_SECRET_KEY"], "")

    def test_unknown_key_is_not_written(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example = root / ".env.example"
            target = root / ".env"
            example.write_text("BPMUSIC_PORT=5000\n", encoding="utf-8")
            document = EnvDocument(target, example)
            document.update({"DANGEROUS_UNKNOWN_KEY": "value"})
            self.assertNotIn("DANGEROUS_UNKNOWN_KEY", target.read_text(encoding="utf-8"))


class ServerConfigDocumentTests(unittest.TestCase):
    def test_update_preserves_cfg_comments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example = root / "myServerconfig.example.cfg"
            target = root / "myServerconfig.cfg"
            example.write_text(
                "# 默认地图\nsv_map \"a\"\nsv_register 0\nsv_rcon_password \"old-secret\"\n",
                encoding="utf-8",
            )
            document = ServerConfigDocument(target, example)
            document.update({"sv_map": "new map", "sv_register": "1", "sv_rcon_password": ""})
            text = target.read_text(encoding="utf-8")
            self.assertIn("# 默认地图", text)
            self.assertIn('sv_map "new map"', text)
            self.assertIn("sv_register 1", text)
            self.assertIn('sv_rcon_password "old-secret"', text)


if __name__ == "__main__":
    unittest.main()
