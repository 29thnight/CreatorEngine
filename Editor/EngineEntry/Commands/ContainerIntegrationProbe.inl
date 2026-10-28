// Runs against the same named module and ownership ABI as the live Editor.
static CommandCore::CommandResult Cmd_container_integration_probe(const ConsoleCommandContext&)
{
    using namespace CommandCore;
    static_assert(ce::detail::is_void<void>);
    static_assert(!ce::detail::is_void<const void>);
    static_assert(!ce::detail::is_void<int>);
    struct Item { uint64_t id{}; own::shared_owner<uint64_t> owner; };
    constexpr size_t producers = 4, count = 2048, total = producers * count;
    ce::lock_free_queue<Item> queue;
    ce::dynamic_array<unsigned char> seen;
    seen.resize(total);
    std::atomic<size_t> done{};
    std::atomic<bool> failed{};
    ce::dynamic_array<std::jthread> workers;
    workers.reserve(producers);
    for (size_t producer = 0; producer < producers; ++producer)
        workers.emplace_back([&, producer] {
            try {
                for (size_t i = 0; i < count; ++i) {
                    const auto id = producer * count + i;
                    queue.push(Item{id, own::make_shared<uint64_t>(id)});
                }
            } catch (...) { failed.store(true); }
            done.fetch_add(1);
        });
    size_t popped = 0;
    bool valid = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        Item item;
        if (queue.try_pop(item)) {
            valid &= item.id < total && item.owner && *item.owner == item.id;
            if (item.id < total) valid &= ++seen[item.id] == 1;
            if ((++popped & 63) == 0) queue.reclaim();
        } else if (done.load() == producers) break;
        else if (std::chrono::steady_clock::now() > deadline) { valid = false; break; }
        else std::this_thread::yield();
    }
    for (auto& worker : workers) worker.join();
    queue.reclaim();
    valid &= !failed.load() && popped == total;
    for (auto value : seen) valid &= value == 1;
    ce::string_hash_map<size_t> map;
    map.insert_or_assign(ce::utf8_string(".png"), size_t(7));
    const auto* value = map.find(ce::utf8_string(".png"));
    ce::utf8_string text(ce::utf8_string_view("a\0b", 3));
    valid &= value && *value == 7 && text.size() == 3 && text[2] == 'b';
    const bool utf16Strings = [&] {
        ce::string sourceText(u"\uD55C\uAE00 \U0001F680");
        ce::utf8_conversion_cache cache;
        if (!cache.update(sourceText.view())) return false;
        ce::string decodedText;
        if (!ce::to_utf16(cache.text().view(), decodedText) || decodedText != sourceText) return false;
        const auto stable = cache.c_str();
        if (!cache.update(sourceText.view()) || cache.c_str() != stable) return false;
        ce::utf16_input_state input;
        if (!input.sync(sourceText.view(), false)) return false;
        input.buffer().assign("\xED\x95\x9C\xEA\xB8\x80");
        return input.commit(decodedText) && decodedText.view() == ce::string_view(u"\uD55C\uAE00");
    }();
    valid &= utf16Strings;
    size_t callbackCount = 0;
    ce::inplace_function<void()> callback([&] { ++callbackCount; });
    callback();
    valid &= callbackCount == 1;
    // Exercise cache lookup and prepared-copy/swap publication through the
    // engine's actual physics runtime, including failed-host rollback.
    ce::physics::CollisionGeometryLibrary geometry;
    uuid::uuid16 asset{};
    asset.data_[0] = 1;
    ce::physics::CollisionGeometrySource source{{asset, 1},
        ce::physics::convex_source{{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}}};
    auto published = geometry.Publish(source);
    auto cached = geometry.Resolve(source.key, [](auto) -> ce::physics::result<ce::physics::CollisionGeometrySource> {
        return std::unexpected(ce::physics::error{ce::physics::error_code::invalid_argument,0,"Unexpected cache miss"});
    });
    source.key.revision = 2;
    auto rejected = geometry.Publish(source, [](auto) { return false; });
    auto stats = geometry.Stats();
    valid &= published && cached && *published == *cached && !rejected && stats &&
        stats->assets == 1 && stats->cache_hits == 1;
    const bool physicsStorage = [&] {
        using namespace ce::physics;
        scene_config config;
        config.workers = 2;
        auto runtime = PhysicsScene::create(config);
        if (!runtime) return false;
        body_definition definition;
        definition.properties.kind = body_kind::dynamic;
        definition.properties.gravity_enabled = false;
        definition.shapes.emplace_back();
        auto copied = body_definition::copy(definition.view());
        if (!copied || copied->shapes.size() != 1) return false;
        auto body = (*runtime)->create_body(copied->view());
        if (!body) return false;
        ce::dynamic_array<std::shared_ptr<const tick_snapshot>> held;
        for (uint64_t step = 1; step <= 8; ++step) {
            const auto state = (*runtime)->status();
            command request{{state.identity, tick_id{state.last_tick.value + 1}, 0, step},
                set_velocity_command{*body, {1,0,0}, {}}};
            if (!(*runtime)->submit_command(std::move(request)) ||
                !(*runtime)->begin_step(1.f / 60) || !(*runtime)->finish_step()) return false;
            auto snapshot = (*runtime)->latest_snapshot();
            if (!snapshot || !snapshot->step_succeeded || snapshot->commands.size() != 1 ||
                snapshot->commands[0].failure || snapshot->active_poses.empty()) return false;
            if (held.size() == 2) held.erase(size_t(0));
            held.push_back(std::move(snapshot));
        }
        auto state = (*runtime)->read_body(*body);
        ce::dynamic_array<query_hit> hits(4);
        auto query = (*runtime)->raycast({0,2,0}, {0,-1,0}, 5, hits);
        if (!state || state->transform.position.x <= 0 || !query || query->written != 1 ||
            hits[0].body != *body) return false;
        auto record = CookedCollisionGeometry::Cook(**runtime, source);
        if (!record || record->payload.empty()) return false;
        auto encoded = CookedCollisionGeometry::Encode(std::span<const cooked_geometry_record>(&*record,1));
        if (!encoded) return false;
        auto decoded = CookedCollisionGeometry::Decode(*encoded, source.key);
        if (!decoded || decoded->payload != record->payload) return false;
        encoded->back() ^= std::byte{1};
        if (CookedCollisionGeometry::Decode(*encoded, source.key)) return false;
        return bool((*runtime)->destroy_body(*body));
    }();
    valid &= physicsStorage;
    std::string heapLog;
    const bool persistentHeapPolicy = RunRHIPersistentHeapPolicyContractTest(heapLog);
    valid &= persistentHeapPolicy;
    const bool drawSealStorage = [] {
        EnhancedDrawSealLedger ledger;
        ledger.Begin(1, 2);
        const EnhancedDrawSealLedger::Binding initial{3, 4, 5, 6};
        if (!ledger.Observe(1, initial)) return false;
        const auto* retained = &ledger.bindings.find(uint64_t{1})->second;
        for (uint64_t key = 2; key <= 64; ++key)
            if (!ledger.Observe(key, initial)) return false;
        if (&ledger.bindings.find(uint64_t{1})->second != retained || !(*retained == initial)) return false;
        auto conflict = initial;
        ++conflict.pipelineId;
        if (ledger.Observe(1, conflict) || ledger.counters.pipelineConflict != 1) return false;
        ledger.Begin(2, 3);
        return ledger.bindings.empty() && ledger.Violations() == 0 && ledger.Observe(1, conflict);
    }();
    valid &= drawSealStorage;
    const bool nodeSetStorage = [] {
        ce::engine_node_set<std::string> keys;
        std::string sourceKey(192, 'x');
        keys.insert(sourceKey);
        const auto original = sourceKey;
        sourceKey.assign(192, 'z');
        const auto* retained = &*keys.find(original);
        keys.reserve(1024);
        if (&*keys.find(std::string_view(original)) != retained) return false;
        for (unsigned i = 0; i != 8192; ++i)
            if (!keys.contains(std::string_view(original)) || keys.contains(std::string_view(sourceKey))) return false;
        auto copied = keys;
        if (copied != keys) return false;
        keys.erase(std::string_view(original));
        if (!keys.empty() || copied.empty()) return false;
        copied.clear();
        return copied.empty();
    }();
    valid &= nodeSetStorage;
    const bool orderedSetStorage = [] {
        ce::engine_ordered_set<unsigned> values;
        for (unsigned i = 0; i != 64; ++i) values.insert(i);
        const auto* stable = &*values.find(31u);
        for (unsigned i = 0; i != 64; i += 2) values.erase(i);
        if (&*values.find(31u) != stable || *values.lower_bound(14u) != 15u ||
            *values.upper_bound(15u) != 17u || *values.rbegin() != 63u) return false;
        struct Entry { unsigned group, sequence; };
        struct Compare { bool operator()(const Entry& a, const Entry& b) const noexcept { return a.group < b.group; } };
        ce::engine_ordered_multiset<Entry, Compare> repeated;
        repeated.insert({7, 1}); repeated.insert({7, 2}); repeated.insert({1, 3});
        auto range = repeated.equal_range(Entry{7, 0});
        if (repeated.count(Entry{7, 0}) != 2 || range.first->sequence != 1) return false;
        ++range.first;
        if (range.first->sequence != 2 || repeated.erase(Entry{7, 0}) != 2) return false;
        return repeated.size() == 1 && repeated.begin()->group == 1;
    }();
    valid &= orderedSetStorage;
    const bool orderedMapStorage = [] {
        ce::engine_ordered_map<unsigned, own::shared_owner<unsigned>> source, target;
        source.try_emplace(7u, own::make_shared<unsigned>(17u));
        target.try_emplace(7u, own::make_shared<unsigned>(99u));
        const auto* stable = &*source.begin();
        auto rejected = target.insert(source.extract(7u));
        if (rejected.inserted || !rejected.node || *rejected.node.mapped() != 17u) return false;
        auto restored = source.insert(std::move(rejected.node));
        if (!restored.inserted || &*restored.position != stable) return false;
        target.erase(7u);
        target.insert(source.extract(source.begin()));
        if (!source.empty() || &*target.begin() != stable || *target.at(7u) != 17u || !target.validate_structure()) return false;
        ce::arena_resource arena;
        ce::arena_map<int, ce::arena_vector<unsigned>> independent;
        {
            ce::arena_map<int, ce::arena_vector<unsigned>> scoped{&arena};
            scoped[1].assign(32, 123u);
            if (scoped.at(1).get_allocator().resource() != &arena) return false;
            independent = scoped;
        }
        arena.reset();
        return !independent.get_allocator().resource() && !independent.at(1).get_allocator().resource() && independent.at(1)[31] == 123u;
    }();
    valid &= orderedMapStorage;
    const bool segmentedStorage = [] {
        ce::segmented_deque<own::shared_owner<unsigned>> values;
        values.push_back(own::make_shared<unsigned>(17u));
        const auto* stable = &values.front();
        for (unsigned i = 0; i < 4096; ++i) values.push_back(own::make_shared<unsigned>(i));
        if (&values.front() != stable || **stable != 17u) return false;
        values.pop_front();
        if (*values.front() != 0u) return false;
        ce::engine_extractable_map<unsigned, own::shared_owner<unsigned>> entries;
        entries.reserve(32);
        entries.try_emplace(1u, own::make_shared<unsigned>(7u));
        const auto* address = &entries.at(1u);
        auto extracted = entries.extract(1u);
        extracted.key() = 2u;
        if (!entries.insert(std::move(extracted)).inserted || &entries.at(2u) != address) return false;
        entries.try_emplace(1u, own::make_shared<unsigned>(99u));
        extracted = entries.extract(2u);
        extracted.key() = 1u;
        auto rejected = entries.insert(std::move(extracted));
        if (rejected.inserted || !rejected.node || *rejected.node.mapped() != 7u) return false;
        rejected.node.key() = 2u;
        return entries.insert(std::move(rejected.node)).inserted && &entries.at(2u) == address;
    }();
    valid &= segmentedStorage;
    const bool funcOwnership = [] {
        struct Counts { unsigned allocations=0, live=0, releases=0; } counts;
        struct Allocator
        {
            Counts* counts;
            void* allocate(size_t bytes,size_t alignment)
            {
                auto* result=ce::heap_allocator{}.allocate(bytes,alignment);
                ++counts->allocations; ++counts->live; return result;
            }
            void deallocate(void* pointer,size_t bytes,size_t alignment) noexcept
            {
                if(!pointer)std::terminate();
                ++counts->releases; --counts->live;
                ce::heap_allocator{}.deallocate(pointer,bytes,alignment);
            }
        };
        using Function=ce::owned_func<int(),Allocator>;
        Function value(std::allocator_arg,Allocator{&counts},[n=0]()mutable{return ++n;});
        auto independent=value;
        if(value()!=1 || independent()!=1 || counts.allocations)return false;
        auto target=[n=0]()mutable{return ++n;};
        ce::func_ref<int()> borrowed=target;
        if(borrowed()!=1 || target()!=2)return false;
        ce::inline_unique_func<int(),64> bounded=[&target]{return target();};
        if(bounded()!=3)return false;
        struct Large
        {
            ce::fixed_array<unsigned char,512> padding{};
            own::shared_owner<unsigned> resource;
            explicit Large(own::shared_owner<unsigned> owner):resource(std::move(owner)){}
            int operator()()const{return static_cast<int>(*resource);}
        };
        auto resource=own::make_shared<unsigned>(17u);
        own::weak_owner<unsigned> weak=resource;
        {
            Function heap(std::allocator_arg,Allocator{&counts},Large{std::move(resource)});
            const auto before=counts.allocations;
            const auto* identity=heap.target<Large>();
            for(unsigned i=0;i<1024;++i)
                if(!heap || heap()!=17 || heap.target<Large>()!=identity)return false;
            if(counts.allocations!=before)return false;
            ce::unique_func<int(),Allocator> transfer=std::move(heap);
            if(heap || transfer.target<Large>()!=identity || transfer()!=17 || weak.expired())return false;
            transfer.reset();
            if(!weak.expired() || counts.live || counts.releases!=1)return false;
        }
        return counts.allocations==1 && !counts.live;
    }();
    valid &= funcOwnership;
    const auto* ifcMode = Meta::Find("EngineMode");
    const bool ifcEngineMode = ifcMode && !ifcMode->is_constructible() &&
        ifcMode->fields().empty() && ifcMode->methods().empty() && Meta::Find("EngineMode") == ifcMode;
    valid &= ifcEngineMode;
    if (!valid) return Fail("containers.integration", "Named-module ownership, queue or string/hash regression");
    auto data = CommandData::Object();
    data.Set("producers", CommandData::Int(producers));
    data.Set("consumed", CommandData::Int(popped));
    data.Set("exactlyOnce", CommandData::Bool(true));
    data.Set("geometryRollback", CommandData::Bool(true));
    data.Set("physicsStorage", CommandData::Bool(physicsStorage));
    data.Set("persistentHeapPolicy", CommandData::Bool(persistentHeapPolicy));
    data.Set("drawSealStorage", CommandData::Bool(drawSealStorage));
    data.Set("nodeSetStorage", CommandData::Bool(nodeSetStorage));
    data.Set("orderedSetStorage", CommandData::Bool(orderedSetStorage));
    data.Set("orderedMapStorage", CommandData::Bool(orderedMapStorage));
    data.Set("segmentedStorage", CommandData::Bool(segmentedStorage));
    data.Set("utf16Strings", CommandData::Bool(utf16Strings));
    data.Set("funcOwnership", CommandData::Bool(funcOwnership));
    data.Set("ifcEngineMode", CommandData::Bool(ifcEngineMode));
    data.Set("module", CommandData::String("Creator.Containers"));
    return Ok("Internal container module integration passed", std::move(data));
}
