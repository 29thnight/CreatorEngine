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

    def test_blackboard_arguments_preserve_private_values_in_the_live_object(self):
        record = self.generator.classes["BlackBoard"]
        self.assertEqual(record["fields"], [])
        self.assertFalse(self.mapping("BlackBoard").argument_safe)
        emitted = self.generator.emit_record(record)
        self.assertNotIn("static member Create(", emitted)
        self.assertNotIn("static member FromJson(", emitted)
        for name, methods in (("FSM::Transition", ("operator()",)),
                              ("FSM::FSMState", ("Enter", "Update", "Exit"))):
            owner = self.generator.classes[name]
            for method in owner["methods"]:
                if method["name"] not in methods:
                    continue
                with self.subTest(owner=name, method=method["name"]):
                    analysis = self.generator.method_analysis(method)
                    self.assertEqual(analysis[2:], ("invoke_bound", ""))
                    emitted = "\n".join(self.generator.emit_method(
                        owner, method, method["name"], "probeSpec", analysis))
                    self.assertIn(".AsNativeArgument()", emitted)
                    self.assertIn("this.InvokeBound(probeSpec, arguments", emitted)
                    self.assertNotIn("BlackBoardView(value))).Encode", emitted)

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

    def test_animator_diagnostics_use_owned_outputs_with_exact_pointer_specs(self):
        owner = self.generator.classes["Animator"]
        names = {"GetClipCount", "GetClipName", "GetClipFrameCount", "GetSkeletonSerial",
                 "GetClipDuration", "GetBoneCount", "GetBoneName", "ResolveBoneIndex", "BuildAvatarBoneMasks"}
        methods = [method for method in owner["methods"] if method["name"] in names]
        self.assertEqual({method["name"] for method in methods}, names)
        self.assertEqual(len(methods), 9)
        for method in methods:
            with self.subTest(method=method["name"]):
                analysis = self.generator.method_analysis(method)
                self.assertEqual(analysis[2:], ("invoke_bound", ""))
                self.assertEqual([mapping.shape for mapping in analysis[1][-2:]], ["output", "output"])
                self.assertTrue(all(not mapping.argument_safe for mapping in analysis[1][-2:]))
                emitted = "\n".join(self.generator.emit_method(
                    owner, method, method["name"], "probeSpec", analysis))
                self.assertIn("``outViaExperiment``: byref<bool>", emitted)
                self.assertIn("``outPath``: byref<NativeEnumValue<AnimatorDataPathTag>>", emitted)
                self.assertEqual(emitted.count("NativeCallArgument.FromOutput("), 2)
                self.assertIn("this.InvokeBound(probeSpec, arguments)", emitted)
                output_indices = list(range(len(method["parameters"]) - 2, len(method["parameters"])))
                for index, name in zip(output_indices, ("outViaExperiment", "outPath")):
                    self.assertIn(f"``{name}`` <-", emitted)
                    self.assertIn(f"response.Arguments.[{index}]", emitted)
                spec = self.generator.method_spec(owner, method)
                self.assertIn('"bool *"; "AnimatorDataPath *" |]', spec)
                self.assertTrue(spec.endswith('"value"; "value" |])'))
                entry = self.generator.manifest[-1]
                self.assertEqual(entry["refWriteback"][-2:], output_indices)
                self.assertTrue(all(parameter["nativeBinding"] for parameter in entry["parameters"][-2:]))

    def test_diagnostic_outputs_do_not_enable_other_raw_pointers(self):
        cases = [
            ("GetClipCountOther", "outViaExperiment", "bool *"),
            ("GetClipCount", "value", "bool *"),
            ("GetClipCount", "outViaExperiment", "const bool *"),
            ("GetClipCount", "outViaExperiment", "bool **"),
            ("GetClipCount", "outViaExperiment", "unsigned char *"),
            ("GetClipCount", "outPath", "bool *"),
            ("GetClipCount", "outPath", "const AnimatorDataPath *"),
            ("GetClipCount", "outViaExperiment", "AnimatorDataPath *"),
        ]
        for method_name, parameter_name, spelling in cases:
            with self.subTest(method=method_name, parameter=parameter_name, spelling=spelling):
                method = self.method(spelling)
                method["name"] = method_name
                method["parameters"][0]["name"] = parameter_name
                analysis = self.generator.method_analysis(method)
                self.assertIn("raw pointer", analysis[3])
                emitted = "\n".join(self.generator.emit_method(
                    self.generator.classes["Scene"], method, method_name, "probeSpec", analysis))
                self.assertIn("_Unavailable", emitted)
                self.assertNotIn("NativeCallArgument.FromOutput(", emitted)
        for spelling in ("bool *", "AnimatorDataPath *"):
            self.assertFalse(self.mapping(spelling).supported)

    def test_height_samples_use_a_bounded_live_sequence_path(self):
        method = {"name": "GetHeightSamples", "signature": "std::span<const float> () const",
                  "return_type": self.generator.resolve("std::span<const float>"),
                  "is_const": True, "parameters": [], "attributes": []}
        owner = self.generator.classes["TerrainComponent"]
        analysis = self.generator.method_analysis(method)
        self.assertEqual(analysis[2:], ("borrow", ""))
        emitted = "\n".join(self.generator.emit_method(
            owner, method, "GetHeightSamples", "probeSpec", analysis))
        self.assertIn("NativeArray<single>", emitted)
        self.assertIn("this.Borrow(probeSpec, arguments)", emitted)
        self.assertNotIn("FromSnapshot", emitted)
        self.assertEqual(self.generator.manifest[-1]["resultLifetime"], "live_path")
        self.assertEqual(self.generator.manifest[-1]["operation"], "borrow")
        variants = [dict(method, name="OtherSamples"), dict(method, is_const=False),
                    dict(method, parameters=[{"name": "index", "type": self.generator.resolve("int")}]),
                    dict(method, return_type=self.generator.resolve("float")),
                    dict(method, attributes=[{"scope": "reflgen", "name": "script_once"}])]
        for variant in variants:
            with self.subTest(variant=variant):
                self.assertEqual(self.generator.method_analysis(variant)[2:], ("invoke", ""))
        raw = next(method for method in owner["methods"] if method["name"] == "GetHeightMap")
        self.assertIn("raw pointer", self.generator.method_analysis(raw)[3])

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
