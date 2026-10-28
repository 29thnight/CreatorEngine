module TypedSupportTests

open System
open System.Text.Json
open CreatorEngine

type private EnumTag = class end

let private snapshot (json: string) =
    use value = JsonDocument.Parse(json)
    NativeTypeView.FromSnapshot(value.RootElement)

let run (check: bool -> string -> unit) (rejected: (unit -> unit) -> string -> string) (native: NativeObject) =
    // Detached results retain their native wire shape and can never become mutable.
    let aliased = snapshot "{\"wire_count\":37}"
    check (aliased.Field("count", "wire_count").Read(NativeCodecs.Int32) = 37) "typed snapshot uses serialized field aliases"
    check (rejected (fun () -> aliased.Field("count", "wire_count").Write(NativeCodecs.Int32, 1)) "typed snapshot write rejected" = "read_only") "typed snapshot write reason"
    let tuple = (snapshot "[4,\"한글\",true]").Read(NativeCodecs.Tuple(NativeCodecs.Int32, NativeCodecs.String, NativeCodecs.Bool))
    check (tuple.Item1 = 4 && tuple.Item2 = "한글" && tuple.Item3) "typed tuple preserves distinct element types"
    rejected (fun () -> tuple.Item1 <- 5) "typed detached tuple is read-only" |> ignore
    let sequence = (snapshot "[2,3,5]").Read(NativeCodecs.Array(NativeCodecs.Int32))
    check (sequence.Count = 3 && sequence.ToArray() = [|2;3;5|]) "typed snapshot sequence"
    rejected (fun () -> sequence.[0] <- 6) "typed detached array is read-only" |> ignore
    let textMap = (snapshot "{\"키\":7}").Read(NativeCodecs.Map(NativeCodecs.String, NativeCodecs.Int32))
    check (textMap.Keys = [|"키"|] && textMap.["키"] = 7 && textMap.TryGetValue("absent").IsNone) "typed map UTF-8 keys and missing lookup"
    let integerMap = (snapshot "{\"18446744073709551615\":\"max\"}").Read(NativeCodecs.Map(NativeCodecs.UInt64, NativeCodecs.String))
    check (integerMap.Keys = [|UInt64.MaxValue|] && integerMap.[UInt64.MaxValue] = "max") "typed map preserves full unsigned key range"
    let pairMap = (snapshot "[[\"키\",8],[\"other\",9]]").Read(NativeCodecs.Map(NativeCodecs.String, NativeCodecs.Int32))
    check (pairMap.["키"] = 8 && pairMap.Keys.Length = 2) "typed map accepts pair-array wire representation"
    let empty = (snapshot "null").Read(NativeCodecs.Optional(NativeCodecs.Int32))
    check (not empty.HasValue && empty.TryValue.IsNone) "typed empty optional"
    let present = (snapshot "17").Read(NativeCodecs.Reference(NativeCodecs.Int32))
    check (present.HasValue && present.Value = 17) "typed detached reference payload"
    let success = (snapshot "{\"value\":null}").Read(NativeCodecs.Expected(NativeCodecs.Unit, NativeCodecs.Int32))
    check (success.IsSuccess && success.TryValue = Some () && success.TryError.IsNone) "typed expected<void> success"
    let failure = (snapshot "{\"error\":31}").Read(NativeCodecs.Expected(NativeCodecs.String, NativeCodecs.Int32))
    check (not failure.IsSuccess && failure.Error = 31 && failure.TryValue.IsNone) "typed expected error branch"
    let variant = (snapshot "[8,18446744073709551615]").Read(NativeCodecs.Variant(
        NativeCodecs.Bool, NativeCodecs.Int32, NativeCodecs.UInt32, NativeCodecs.Single,
        NativeCodecs.Double, NativeCodecs.String, NativeCodecs.Int16, NativeCodecs.Int64, NativeCodecs.UInt64))
    check (variant.Index = 8 && variant.Case1.IsNone && variant.Case9 = Some UInt64.MaxValue) "typed nine-alternative variant preserves discriminator and width"
    rejected (fun () -> variant.SetCase2(9)) "typed detached variant is read-only" |> ignore
    let enumCodec = NativeCodecs.Enum<EnumTag>("ProbeEnum")
    let name = NativeEnumValue<EnumTag>.FromName("ProbeEnum", "Running")
    check ((enumCodec.Encode(name)).GetString() = "Running") "typed symbolic enum"
    let unknown = NativeEnumValue<EnumTag>.FromUInt64("ProbeEnum", UInt64.MaxValue)
    check ((enumCodec.Decode(NativeTypeView.FromSnapshot(enumCodec.Encode(unknown)))).TryUInt64 = Some UInt64.MaxValue) "typed enum preserves unknown unsigned flags"
    rejected (fun () -> enumCodec.Encode(NativeEnumValue<EnumTag>.FromInt64("WrongEnum", 1L)) |> ignore) "typed enum rejects a different native identity" |> ignore
    let guid = Guid.Parse("92a68067-05d2-4cb5-b5c4-07a59cc57a89")
    check (NativeCodecs.Guid.Decode(NativeTypeView.FromSnapshot(NativeCodecs.Guid.Encode(guid))) = guid) "typed UUID wire roundtrip"
    let math = Float3(1.25f, -2.5f, 3.75f)
    let mathJson = NativeCodecs.Float3.Encode(math)
    check (mathJson.GetProperty("x").GetSingle() = math.X && NativeCodecs.Float3.Decode(NativeTypeView.FromSnapshot(mathJson)).Z = math.Z) "typed math codec uses native lowercase fields"

    // The same helpers operate on identity-checked production native paths.
    let root = NativeTypeView.FromNative(native)
    let liveAlias = root.Field("aliased", "aliased").Field("count", "wire_count")
    liveAlias.Write(NativeCodecs.Int32, 73)
    check (liveAlias.Read(NativeCodecs.Int32) = 73) "typed live field uses native name, not serialized key"
    let recordCodec = NativeCodecs.View(id)
    let nativeTuple = root.Field("tuple", "tuple").Read(NativeCodecs.Tuple(NativeCodecs.Int32, recordCodec, NativeCodecs.String))
    check (nativeTuple.Item1 = 5 && nativeTuple.Item3 = "tuple") "typed live tuple selection"
    nativeTuple.Item1 <- 6
    nativeTuple.Item2.Field("count", "count").Write(NativeCodecs.Int32, 38)
    check (nativeTuple.Item1 = 6 && nativeTuple.Item2.Field("count", "count").Read(NativeCodecs.Int32) = 38) "typed live tuple mutation"
    let nativeMap = root.Field("engineNamed", "engineNamed").Read(NativeCodecs.Map(NativeCodecs.String, recordCodec))
    check (nativeMap.Keys |> Array.contains "엔진") "typed actual engine map keys"
    nativeMap.["엔진"].Field("count", "count").Write(NativeCodecs.Int32, 74)
    check (nativeMap.["엔진"].Field("count", "count").Read(NativeCodecs.Int32) = 74) "typed actual engine map nested mutation"
    let chooseInt = NativeMethodSpec("ProbeComponent", "Choose", true, [|"int"|], [|"value"|])
    let chooseFloat = NativeMethodSpec("ProbeComponent", "Choose", true, [|"float"|], [|"value"|])
    check ((root.Invoke(chooseInt, [|NativeCodecs.Int32.Encode(3)|])).Read<int>() = 13) "typed overload resolves integer metadata"
    check ((root.Invoke(chooseFloat, [|NativeCodecs.Single.Encode(3.0f)|])).Read<int>() = 23) "typed overload resolves floating metadata"
    check ((root.Invoke(chooseInt, [|NativeCodecs.Int32.Encode(4)|])).Read<int>() = 14) "typed successful metadata cache preserves overload"
    let mutableSpan = NativeMethodSpec("ProbeComponent", "ChooseSpan", true, [|"std::span<int>"|], [|"value"|])
    let constSpan = NativeMethodSpec("ProbeComponent", "ChooseSpan", true, [|"std::span<const int>"|], [|"value"|])
    let spanInput = (snapshot "[1,2]").ReadJson()
    check ((root.Invoke(mutableSpan, [|spanInput|])).Read<int>() = 102) "typed selector preserves mutable template argument"
    check ((root.Invoke(constSpan, [|spanInput|])).Read<int>() = 202) "typed selector preserves const template argument"
    let msvcMutableSpan = NativeMethodSpec("ProbeComponent", "ChooseSpan", true, [|"std::span<int,0xffffffffffffffffui64>"|], [|"value"|])
    let msvcConstSpan = NativeMethodSpec("ProbeComponent", "ChooseSpan", true, [|"std::span<int const,18446744073709551615ui64>"|], [|"value"|])
    check ((root.Invoke(msvcMutableSpan, [|spanInput|])).Read<int>() = 102) "typed selector normalizes MSVC hex dynamic extent"
    check ((root.Invoke(msvcConstSpan, [|spanInput|])).Read<int>() = 202) "typed selector normalizes trailing base const without losing constness"
    let graph = NativeMethodSpec("ProbeComponent", "GraphAlternative", true,
        [|"std::variant<std::monostate,bool,long long,double,ce::fixed_array<double,0x3>,ce::fixed_array<double,0x4>,std::basic_string<char>>"|], [|"value"|])
    check ((root.Invoke(graph, [|(snapshot "[4,[1,2,3]]").ReadJson()|])).Read<int>() = 4) "typed selector normalizes actual material-style nested fixed-array extents"
    let wrongExtent = NativeMethodSpec("ProbeComponent", "ChooseSpan", true, [|"std::span<int,0x3>"|], [|"value"|])
    check (rejected (fun () -> root.Invoke(wrongExtent, [|spanInput|]) |> ignore) "fixed extent cannot become dynamic" = "method_not_found") "typed selector preserves unequal extent identity"
    let trailingPointee = NativeMethodSpec("ProbeComponent", "EchoCString", true, [|"char const *"|], [|"value"|])
    check ((root.Invoke(trailingPointee, [|NativeCodecs.String.Encode("const")|])).Read<string>() = "const") "typed selector preserves const pointee with MSVC qualifier spelling"
    let echo = NativeMethodSpec("ProbeComponent", "Echo", true,
        [|"std::basic_string<char, std::char_traits<char>, std::allocator<char>>"|], [|"const_reference"|])
    check ((root.Invoke(echo, [|NativeCodecs.String.Encode("선택 🎹")|])).Read<string>() = "선택 🎹") "typed template normalization resolves string overload"
    let getter = NativeMethodSpec("ProbeComponent", "GetSettings", false, [||], [||])
    let borrowed = root.Borrow(getter)
    borrowed.Field("count", "count").Write(NativeCodecs.Int32, 75)
    check (native.["settings"].["count"].Read<int>() = 75) "typed borrowed getter retains writable live identity"
    let readonlyGetter = NativeMethodSpec("ProbeComponent", "GetReadOnlySettings", true, [||], [||])
    rejected (fun () -> root.Borrow(readonlyGetter).Field("count", "count").Write(NativeCodecs.Int32, 1)) "typed const getter remains read-only" |> ignore
    let snapshotGetter = NativeMethodSpec("ProbeComponent", "SnapshotSettings", true, [||], [||])
    let detached = NativeTypeView.FromSnapshot(root.Invoke(snapshotGetter, [||]).ReturnValue)
    rejected (fun () -> detached.Field("count", "count").Write(NativeCodecs.Int32, 1)) "typed returned record is a read-only snapshot" |> ignore
    let nativeVariant = root.Field("choice", "choice").Read(NativeCodecs.Variant(recordCodec, NativeCodecs.Int32))
    let previousCase = nativeVariant.Case1.Value
    check (previousCase.Field("count", "count").Read(NativeCodecs.Int32) = 33) "typed variant live record alternative"
    nativeVariant.SetCase2(91)
    check (nativeVariant.Case2 = Some 91) "typed variant replacement preserves active branch"
    check (rejected (fun () -> previousCase.Field("count", "count").Read(NativeCodecs.Int32) |> ignore) "old typed variant alternative is rejected" = "inactive_alternative") "typed variant path cannot retarget another case"
    let nativeExpected = root.Field("outcome", "outcome").Read(NativeCodecs.Expected(NativeCodecs.Int32, recordCodec))
    let previousError = nativeExpected.Error
    check (previousError.Field("count", "count").Read(NativeCodecs.Int32) = 44) "typed expected error live record"
    nativeExpected.SetValue(92)
    check (nativeExpected.IsSuccess && nativeExpected.Value = 92) "typed expected replacement"
    check (rejected (fun () -> previousError.Field("count", "count").Read(NativeCodecs.Int32) |> ignore) "old typed expected error branch rejected" = "inactive_alternative") "typed expected path cannot retarget its result"
    let stale = root.Field("settings", "settings").Field("count", "count")
    fun () -> stale.Read(NativeCodecs.Int32) |> ignore
