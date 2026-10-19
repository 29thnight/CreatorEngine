#ifndef PRSM_MOCK_RELOAD_BRIDGE_H
#define PRSM_MOCK_RELOAD_BRIDGE_H

#include "prsm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draft mock-host reload profile, independently versioned from CurrentABI=2.
 * This header does not extend or change any engine ABI structure. All pointers
 * belong to the exact loaded generation identified by schema.module; retain
 * that generation while inspecting metadata or invoking its callbacks. */
#define PRSM_RELOAD_SCHEMA_VERSION 1u
#define PRSM_RELOAD_FIELD_SERIALIZED 1u
#define PRSM_RELOAD_FIELD_IMMUTABLE 2u

/* Scalar IDs match the existing PrsmFieldDesc scalar IDs, but values cross the
 * reload boundary explicitly. Signed integers use declared-width two's
 * complement bits, zero-extended to uint64_t; bool accepts only zero or one.
 * Float32 uses raw IEEE binary32 bits zero-extended to uint64_t (upper bits
 * must be zero); float64 uses raw IEEE binary64 bits. All NaN payloads, signed
 * zero and subnormal representations are preserved without FP arithmetic.
 * Thus uint64_t values never round through signed integers or floating point.
 * On failure, read leaves out_bits untouched and write leaves object untouched.
 * field_index indexes this component's serialized fields, not object offsets.
 * Reads require the owning module's active game-thread ScriptScope. Writes
 * additionally require the coordinator's object-specific restore privilege;
 * they do not grant ordinary source/native callers mutability of val fields. */
typedef PrsmCallStatus (*PrsmReloadReadFn)(void* object, uint32_t field_index,
                                         uint64_t* out_bits);
typedef PrsmCallStatus (*PrsmReloadWriteFn)(void* object, uint32_t field_index,
                                          uint64_t bits);

typedef struct PrsmReloadField {
    CeStr name;
    uint32_t type_id;
    uint32_t flags;
} PrsmReloadField;

typedef struct PrsmReloadComponent {
    /* Code ownership metadata; not part of the serialized storage identity. */
    CeStr module_name;
    /* Namespace-qualified source component name, also descriptor->name.
     * Duplicate canonical names across project modules are rejected. */
    CeStr canonical_name;
    const PrsmTypeDesc* type;
    uint32_t field_count;
    uint32_t reserved;
    const PrsmReloadField* fields;
    PrsmReloadReadFn read;
    PrsmReloadWriteFn write;
} PrsmReloadComponent;

typedef struct PrsmReloadBinding {
    CeStr symbol;
    uint8_t digest[32];
} PrsmReloadBinding;

typedef struct PrsmReloadSchema {
    uint32_t version;
    uint32_t struct_size;
    const PrsmModule* module;
    /* Domain-separated SHA-256 of the emitted project's public callable and
     * component interface. Not an engine ABI fingerprint or build cache key.
     * An interface change alone is not a state migration incompatibility. */
    uint8_t compile_interface_identity[32];
    uint8_t engine_fingerprint[32];
    uint32_t binding_count;
    uint32_t component_count;
    const PrsmReloadBinding* bindings;
    const PrsmReloadComponent* components;
} PrsmReloadSchema;

typedef const PrsmReloadSchema* (*PrsmReloadGetFn)(void);

/* Exported by compiler-generated mock-profile modules. The host validates the
 * complete candidate schema and required bindings before old tasks cancel. */
const PrsmReloadSchema* prsm_reload_get(void);

/* Mock-only checked GC query. It never dereferences unrecognized object bits.
 * OK requires a live base allocation with this exact descriptor and a current
 * ScriptScope for its registered owning generation. No engine ABI additions. */
CE_API PrsmCallStatus prsm_host_reload_validate_object(
    void* object, const PrsmTypeDesc* expected_type);

/* In addition to the read query's checks, requires an object-specific restore
 * privilege held internally by the generation coordinator. It is unavailable
 * for ordinary writes to current/published or retired component instances. */
CE_API PrsmCallStatus prsm_host_reload_validate_restore(
    void* object, const PrsmTypeDesc* expected_type);

#ifdef __cplusplus
}
#endif
#endif
