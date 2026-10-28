# Node-map ablation

This is a generated experiment, not another canonical implementation. `generate.ps1`
reads CreatorEngine/Engine/Containers/modules/node_hash_map.ixx, records its hash,
and derives one experimental module with two independent compile-time switches.
The supporting containers, hashing and allocation import the engine's canonical
Creator.Containers module. The production node_hash_map is also timed as a control.

Build with CE_MODULES=ON and CE_NODE_MAP_ABLATION=ON in an isolated build directory.
Run all four node_map_probe_test_* targets, then the separate counter executable.
Run measure.ps1 only after builds and other tests have stopped. It freezes inputs,
archives the executable, measures all five variants in three processes each, and
retains all samples. The option defaults to OFF. Do not hand-edit generated files.
