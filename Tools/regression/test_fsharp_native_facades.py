#!/usr/bin/env python3
"""Regression cases for actual runtime adapters and nested native type spellings."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("fsharp_facades", ROOT / "Tools/build/generate-fsharp-native-facades.py")
GEN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GEN
SPEC.loader.exec_module(GEN)


class NativeFacadeMappings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generator = GEN.Generator(json.loads(
            (ROOT / "ScriptNative/GeneratedNative/native-surface.json").read_text(encoding="utf-8")))

    def mapping(self, spelling):
        return self.generator.mapping(self.generator.resolve(spelling))

    def method(self, spelling):
        return {"name": "AcceptProbe", "signature": f"void AcceptProbe({spelling})",
                "return_type": self.generator.resolve("void"), "is_const": False,
                "parameters": [{"name": "value", "type": self.generator.resolve(spelling)}],
                "attributes": []}

    def test_const_pointer_element_keeps_the_native_pointee(self):
        mapping = self.mapping("ce::dynamic_array<assets::ModelAnimationTrack *const>")
        self.assertTrue(mapping.supported, mapping.reason)
        self.assertEqual(mapping.fs_type, "NativeArray<NativeReference<assets_ModelAnimationTrackView>>")
        self.assertFalse(mapping.argument_safe)
        self.assertEqual(GEN.unqualified("const assets::ModelAnimationTrack *const"),
                         "const assets::ModelAnimationTrack *")

    def test_asset_links_keep_typed_identity_and_conversion(self):
        mapping = self.mapping("AssetDepot::AssetLink<Material>")
        self.assertTrue(mapping.supported, mapping.reason)
        record = self.generator.classes["AssetDepot::AssetLink<Material>"]
        self.assertEqual(record["fields"][0]["type"]["canonical_spelling"], "experiment::cooked::AssetIdentity")
        self.assertEqual({method["name"] for method in record["methods"]}, {"IsValid", "ToReference"})

    def test_layers_preserve_id_and_error_types(self):
        self.assertEqual(self.mapping("ce::layers::layer_id").fs_type, "ce_layers_layer_idView")
        self.assertEqual(self.mapping("ce::layers::error").fs_type, "NativeEnumValue<ce_layers_errorTag>")

    def test_completion_and_subscription_values_are_read_only(self):
        for spelling in ("std::atomic<bool>", "ce::delegate_handle"):
            with self.subTest(spelling=spelling):
                mapping = self.mapping(spelling)
                self.assertTrue(mapping.supported, mapping.reason)
                self.assertFalse(mapping.writable)
                self.assertFalse(mapping.argument_safe)
        mapping = self.mapping("own::shared_owner<std::atomic<bool>>")
        self.assertTrue(mapping.supported, mapping.reason)
        self.assertEqual(mapping.fs_type, "NativeReference<bool>")

    def test_file_clock_is_exact_ticks_and_ordering_is_signed(self):
        for spelling in ("std::filesystem::file_time_type",
                         "std::chrono::time_point<std::filesystem::_File_time_clock>",
                         "std::chrono::time_point<std::filesystem::__file_clock>"):
            with self.subTest(spelling=spelling):
                mapping = self.mapping(spelling)
                self.assertEqual(mapping.fs_type, "int64")
                self.assertTrue(mapping.writable)
        self.assertEqual(self.mapping("std::strong_ordering").fs_type, "int")
        self.assertFalse(self.mapping("std::chrono::time_point<UnknownClock>").supported)
        self.assertFalse(self.mapping(
            "std::chrono::time_point<std::filesystem::_File_time_clock, std::chrono::seconds>").supported)

    def test_scene_services_require_existing_native_identity(self):
        for name in ("CameraSystem", "SoundSystem", "TransformStore", "HierarchyStore",
                     "SystemSchedule", "ScenePhysicsSimulation", "BasicTweenManager<Scene>", "TextureFramePins"):
            with self.subTest(name=name):
                mapping = self.mapping(name)
                self.assertTrue(mapping.supported, mapping.reason)
                self.assertFalse(mapping.argument_safe)
                emitted = self.generator.emit_record(self.generator.classes[name])
                self.assertNotIn("static member Create(", emitted)
                self.assertNotIn("static member FromJson(", emitted)
        store = self.generator.classes["TransformStore"]
        self.assertTrue(all("reflgen::readonly" in self.generator.attributes(field) for field in store["fields"]))

    def test_clip_helpers_preserve_the_native_identity_kind(self):
        emitted = self.generator.emit_record(self.generator.classes["wave::ClipKey"])
        self.assertIn("static member CreateText(text: string)", emitted)
        self.assertIn("static member CreateGuid(guid: System.Guid)", emitted)
        self.assertIn("JsonSerializer.SerializeToElement(false)", emitted)
        self.assertIn("JsonSerializer.SerializeToElement(true)", emitted)
        self.assertNotIn("static member Create()", emitted)

    def test_state_machine_children_cannot_be_constructed_from_empty_json(self):
        for name in ("FSM::FSMState", "FSM::Transition"):
            with self.subTest(name=name):
                self.assertFalse(self.mapping(name).argument_safe)
                emitted = self.generator.emit_record(self.generator.classes[name])
                self.assertNotIn("static member Create(", emitted)
                self.assertNotIn("static member FromJson(", emitted)

    def test_unsafe_scalar_inputs_do_not_emit_native_view_members(self):
        for spelling in ("std::atomic<bool>", "ce::delegate_handle", "std::strong_ordering", "ce::string_view"):
            with self.subTest(spelling=spelling):
                method = self.method(spelling)
                analysis = self.generator.method_analysis(method)
                self.assertIn("live typed view", analysis[3])
                emitted = "\n".join(self.generator.emit_method(
                    self.generator.classes["Scene"], method, "AcceptProbe", "probeSpec", analysis))
                self.assertIn("AcceptProbe_Unavailable", emitted)
                self.assertNotIn(".AsNativeArgument()", emitted)

    def test_native_pointer_inputs_keep_the_wrapper_for_typed_null(self):
        method = self.method("CameraComponent *")
        analysis = self.generator.method_analysis(method)
        self.assertEqual(analysis[2:], ("invoke_bound", ""))
        emitted = "\n".join(self.generator.emit_method(
            self.generator.classes["Scene"], method, "AcceptProbe", "probeSpec", analysis))
        self.assertIn("``value``.AsNativeArgument()", emitted)
        self.assertNotIn(".Dereference()", emitted)

    def test_rooted_callback_input_uses_its_registry_token(self):
        method = self.method("ce::func_detail::owning_func<void, false, true, false, 48, 8, ce::heap_allocator, float>")
        analysis = self.generator.method_analysis(method)
        self.assertEqual(analysis[2:], ("invoke_bound", ""))
        emitted = "\n".join(self.generator.emit_method(
            self.generator.classes["Scene"], method, "AcceptProbe", "probeSpec", analysis))
        self.assertIn("NativeCallback<single>", emitted)
        self.assertIn("NativeCallArgument.FromCallback(``value``.Token)", emitted)
        self.assertNotIn(".AsNativeArgument()", emitted)

    def test_state_machine_pointer_results_capture_identity_without_replaying_lookup(self):
        for spelling in ("FSM::FSMState *", "FSM::Transition *"):
            with self.subTest(spelling=spelling):
                method = self.method("const std::string &")
                method["return_type"] = self.generator.resolve(spelling)
                method["is_const"] = True
                analysis = self.generator.method_analysis(method)
                self.assertEqual(analysis[2:], ("invoke_identity", ""))
                emitted = "\n".join(self.generator.emit_method(
                    self.generator.classes["Scene"], method, "FindProbe", "probeSpec", analysis))
                self.assertIn("let response, value = this.InvokeBoundReference(probeSpec, arguments)", emitted)
                self.assertNotIn("this.Borrow(", emitted)
                self.assertNotIn("FromSnapshot(response.ReturnValue)", emitted)


if __name__ == "__main__":
    unittest.main()
