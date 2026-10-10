"""Offline converter contract sources. Not executed during implementation."""
import copy
import pathlib
import tempfile
import unittest

import migrate_inputmap as migration


class MigrationContractTests(unittest.TestCase):
    def setUp(self):
        self.source = pathlib.Path(__file__).parent / "fixtures" / "legacy" / "PlayerMenuKey.inputmap"
        report = migration.inspect(self.source)
        self.review = {
            "graphGuid": "9ad58e30-9ff7-4f2a-a8c2-435a3c126801",
            "sourceSha256": report["sourceSha256"],
            "actions": {"Menu": {"valueType": "Button", "domain": "UI", "claim": "OnPress",
                "consumerReviewed": True, "consumerFile": "ReviewedMenu.cs", "consumerSymbol": "ReadMenu",
                "dispatch": "ReadHeld"}}}

    def test_corpus_reports_missing_value_type_and_callbacks(self):
        reports = [migration.inspect(path) for path in self.source.parent.glob("*.inputmap")]
        self.assertEqual(len(reports), 6)
        actions = [action for report in reports for action in report["actions"]]
        self.assertEqual(len(actions), 26)
        self.assertEqual(sum(action["valueType"] == "Unresolved" for action in actions), 4)
        self.assertTrue(all(action["consumerStatus"] == "RequiresExplicitReview" for action in actions))

    def test_rejects_unreviewed_callback(self):
        review = copy.deepcopy(self.review)
        review["actions"]["Menu"]["consumerReviewed"] = False
        with self.assertRaises(ValueError):
            migration.convert(self.source, review)

    def test_rejects_changed_source(self):
        review = copy.deepcopy(self.review)
        review["sourceSha256"] = "0" * 64
        with self.assertRaises(ValueError):
            migration.convert(self.source, review)

    def test_requires_all_actions(self):
        review = copy.deepcopy(self.review)
        review["actions"] = {}
        with self.assertRaises(ValueError):
            migration.convert(self.source, review)

    def test_stable_conversion_and_callback_report(self):
        source, report = migration.convert(self.source, self.review)
        self.assertEqual((source, report), migration.convert(self.source, self.review))
        self.assertTrue(source.startswith('LXINPUT 1 "9ad58e30-9ff7-4f2a-a8c2-435a3c126801"'))
        self.assertNotIn("OberveMenuKeyPress", source)
        self.assertEqual(report["consumers"][0]["legacyCallback"], "MenuKeyObserver.OberveMenuKeyPress")
        self.assertEqual(report["consumers"][0]["dispatch"], "ReadHeld")

    def test_unknown_legacy_enum_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "unknown.inputmap"
            path.write_text(self.source.read_text().replace('"GamePad"', '"FutureDevice"'))
            with self.assertRaises(ValueError):
                migration.inspect(path)

    def test_unknown_field_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "unknown.inputmap"
            path.write_text(self.source.read_text() + '    inferredValueType: "Button"\n')
            with self.assertRaises(ValueError):
                migration.inspect(path)


if __name__ == "__main__":
    unittest.main()
