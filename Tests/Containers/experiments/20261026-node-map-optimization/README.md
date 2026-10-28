# Node map allocation/index experiment

The production implementation is CreatorEngine/Engine/Containers/modules/node_hash_map.ixx.
This folder preserves the staged optimization and allocation/index controls. Its
CMake option defaults to OFF. The production acceptance consumer is
tests/node_map_acceptance.cpp and imports Creator.Containers directly.

Historical baseline runs must use the recorded pre-promotion snapshot via
CE_IMPLEMENTATION_ROOT in a separate build directory. The generator rejects a
changed baseline epoch so the optimized canonical map cannot silently become the
old control. Each measurement receipt archives its inputs and executable.

The pool-only allocator is a single-threaded diagnostic policy, not an engine
allocator. The adopted map owns its pool directly with its byte allocator and
does not use the diagnostic allocator's shared state.
