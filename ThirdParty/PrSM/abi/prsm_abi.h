/*
 * prsm_abi.h — PrSM Script ABI (draft v2, M0 test host)
 *
 * 엔진(또는 테스트 하네스)과 PrSM 스크립트 DLL 사이의 유일한 C 계약.
 * 이 파일이 원본이고, 컴파일러(Rust)는 bindgen으로 같은 정의를 생성한다.
 *
 * 방향
 *   스크립트 DLL → 호스트 : prsm_module_get() 하나를 export 한다(모듈 표).
 *   호스트 → 스크립트 DLL : ce_* 함수를 export 하고 스크립트가 import 한다.
 *
 * 규칙
 *   - 모든 호출은 GameThread에서만 일어난다.
 *   - GC(collect_step)는 스크립트 코드가 스택에 있는 동안 돌지 않는다.
 *     따라서 스크립트 지역 변수에는 루트도 스택 맵도 없다.
 *   - 예외는 경계를 넘지 않는다. 스크립트 버그는 ce_panic으로 보고한다.
 *   - 구조체 레이아웃은 x64 Windows(MSVC) 기준. 크기 검사는 아래 static_assert.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRSM_ABI_H
#define PRSM_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRSM_ABI_VERSION 2u

#if defined(_WIN32)
#  define PRSM_EXPORT __declspec(dllexport)
#  define PRSM_IMPORT __declspec(dllimport)
#else
#  define PRSM_EXPORT __attribute__((visibility("default")))
#  define PRSM_IMPORT
#endif

#if defined(__cplusplus)
#  define PRSM_NORETURN [[noreturn]]
#elif defined(_MSC_VER)
#  define PRSM_NORETURN __declspec(noreturn)
#else
#  define PRSM_NORETURN _Noreturn
#endif

#ifdef PRSM_HOST_BUILD
#  define CE_API PRSM_EXPORT
#else
#  define CE_API PRSM_IMPORT
#endif

/* ────────────────────────────── 기본 값 타입 ────────────────────────────── */

/* 엔진 객체 핸들. 하위 32비트 슬롯, 상위 32비트 세대. 0은 null.
 * 세대가 맞지 않으면 죽은 객체 — IsAlive가 false, 접근은 fail-closed. */
typedef struct CeHandle { uint64_t bits; } CeHandle;

/* 빌린 UTF-8 문자열 뷰. NUL 종료를 보장하지 않는다.
 * 받는 쪽이 보관하려면 복사해야 한다. */
typedef struct CeStr { const char* ptr; size_t len; } CeStr;

/* PrSM string의 메모리 표현(16바이트). 사용자는 직접 다루지 않는다.
 * tag의 최상위 비트 0: SSO — bytes[0..15]에 최대 15바이트, len = bytes[15]
 * tag의 최상위 비트 1: 긴 문자열 — ptr는 GC 버퍼, len은 하위 63비트 */
typedef union CeString {
    uint8_t bytes[16];
    struct { const char* ptr; uint64_t tag_len; } heap;
} CeString;

/* ────────────────────────────── 훅 마스크 ────────────────────────────── */
/* 비트 자리는 엔진 LifecycleRegistry.h의 Lifecycle::PhaseBits와 같다.
 * 1<<3..1<<5는 철거된 옛 틱 비트 자리라 비워 둔다(재사용 금지).
 * 틱 3종은 엔진에서 네이티브 비트가 없으므로 상위 자리를 새로 쓴다. */
enum {
    PRSM_HOOK_ON_INITIALIZED        = 1u << 0,
    PRSM_HOOK_ON_ENABLE             = 1u << 1,
    PRSM_HOOK_ON_BEGIN_SIMULATION   = 1u << 2,
    PRSM_HOOK_ON_DISABLE            = 1u << 6,
    PRSM_HOOK_ON_UNINITIALIZING     = 1u << 7,
    PRSM_HOOK_ON_ADDED_TO_SCENE     = 1u << 8,
    PRSM_HOOK_ON_END_SIMULATION     = 1u << 9,
    PRSM_HOOK_ON_REMOVING_FROM_SCENE = 1u << 10,
    /* 틱(일괄 디스패치) */
    PRSM_HOOK_PRE_PHYSICS           = 1u << 16,
    PRSM_HOOK_POST_PHYSICS          = 1u << 17,
    PRSM_HOOK_ON_SIMULATE           = 1u << 18  /* suspend 계약 */
};

/* 생명주기 단계 인덱스. 엔진 ScriptLifecyclePhase와 값이 같다. */
typedef enum PrsmPhase {
    PRSM_PHASE_ON_INITIALIZED       = 0,
    PRSM_PHASE_ON_ADDED_TO_SCENE    = 1,
    PRSM_PHASE_ON_BEGIN_SIMULATION  = 2,
    PRSM_PHASE_ON_END_SIMULATION    = 3,
    PRSM_PHASE_ON_REMOVING_FROM_SCENE = 4,
    PRSM_PHASE_ON_UNINITIALIZING    = 5,
    PRSM_PHASE_COUNT                = 6
} PrsmPhase;

/* ────────────────────────────── 필드 테이블 ────────────────────────────── */

typedef enum PrsmTypeId {
    PRSM_T_BOOL = 1,
    PRSM_T_INT8, PRSM_T_INT16, PRSM_T_INT32, PRSM_T_INT64,
    PRSM_T_UINT8, PRSM_T_UINT16, PRSM_T_UINT32, PRSM_T_UINT64,
    PRSM_T_FLOAT32, PRSM_T_FLOAT64,
    PRSM_T_CHAR,            /* 32비트 Unicode scalar */
    PRSM_T_STRING,          /* CeString */
    PRSM_T_VECTOR2, PRSM_T_VECTOR3, PRSM_T_VECTOR4, PRSM_T_QUATERNION,
    PRSM_T_HANDLE,          /* CeHandle — 대상 엔진 타입은 type_name */
    PRSM_T_ENUM,            /* 정수 저장, 케이스 이름은 enum_cases */
    PRSM_T_STRUCT,          /* 중첩 값 타입 — nested */
    PRSM_T_ARRAY            /* std::array<T, N> — element, array_len */
} PrsmTypeId;

enum {
    PRSM_FIELD_SERIALIZE          = 1u << 0,  /* @serialize */
    PRSM_FIELD_REQUIRE            = 1u << 1,  /* @require */
    PRSM_FIELD_OPTIONAL           = 1u << 2,  /* @require + nullable */
    PRSM_FIELD_HIDE_IN_INSPECTOR  = 1u << 3,
    PRSM_FIELD_READ_ONLY_IN_INSPECTOR = 1u << 4,
    PRSM_FIELD_DEBUG_ONLY         = 1u << 5,
    PRSM_FIELD_WIDE               = 1u << 6,
    PRSM_FIELD_HAS_RANGE          = 1u << 7,  /* @range — range_min/max 유효 */
    PRSM_FIELD_IMMUTABLE          = 1u << 8   /* @serialize val */
};

typedef struct PrsmTypeDesc PrsmTypeDesc;

typedef struct PrsmFieldDesc {
    CeStr               name;          /* 선언 이름(PascalCase) */
    CeStr               display_name;  /* @display_name, 없으면 len 0 */
    CeStr               tooltip;       /* @tooltip */
    CeStr               header;        /* @header — 이 필드 앞에 그릴 머리글 */
    CeStr               former_name;   /* @former_name — 직렬화 이름 이전 */
    CeStr               unit;          /* 단위 문자열 "m/s", 없으면 len 0 */
    CeStr               type_name;     /* HANDLE/ENUM/STRUCT 대상 타입 이름 */
    uint32_t            offset;        /* 인스턴스 시작 기준 바이트 오프셋 */
    uint32_t            type_id;       /* PrsmTypeId */
    uint32_t            flags;         /* PRSM_FIELD_* */
    uint32_t            array_len;     /* ARRAY일 때 N */
    double              range_min, range_max;
    const PrsmTypeDesc* nested;        /* STRUCT일 때 */
    const struct PrsmFieldDesc* element; /* ARRAY일 때 원소 설명 */
    const CeStr*        enum_cases;    /* ENUM일 때 */
    uint32_t            enum_case_count;
    uint32_t            reserved;
} PrsmFieldDesc;

/* ────────────────────────────── 코루틴 ────────────────────────────── */
/* suspend 함수의 프레임은 GC 객체다. 호스트 스케줄러는 프레임을 불투명
 * 포인터로만 다루고, 재개·취소는 모듈 표의 함수로 한다. */

typedef enum PrsmTaskStatus {
    PRSM_TASK_SUSPENDED = 0,  /* wait에 대기 조건이 채워져 있음 */
    PRSM_TASK_DONE      = 1,
    PRSM_TASK_CANCELLED = 2,  /* defer 실행 완료 */
    PRSM_TASK_PANICKED  = 3   /* explicit failure status; user defer not run */
} PrsmTaskStatus;

typedef enum PrsmWaitKind {
    PRSM_WAIT_NEXT_FRAME = 0,
    PRSM_WAIT_FRAMES     = 1, /* u.frames */
    PRSM_WAIT_SECONDS    = 2, /* u.seconds (게임 시간) */
    PRSM_WAIT_UNTIL      = 3, /* u.until — 매 프레임 predicate 평가 */
    PRSM_WAIT_TASKS      = 4  /* gather/first — 자식 작업 대기, 스크립트 런타임이 판정 */
} PrsmWaitKind;

typedef struct PrsmWait {
    uint32_t kind;  /* PrsmWaitKind */
    uint32_t reserved;
    union {
        uint32_t frames;
        double   seconds;
        struct { int (*predicate)(void* frame); } until;
    } u;
} PrsmWait;

/* ────────────────────────────── 타입 서술자 ────────────────────────────── */

typedef void (*PrsmHookFn)(void* self);
typedef void (*PrsmTickFn)(void* const* instances, uint32_t count, float dt);

/* Draft v2 test-host extension. A checked callback returns failure explicitly;
 * no exception or non-local jump may cross this ABI. Legacy void callbacks
 * remain callable but ce_panic is fatal and cannot isolate them. */
typedef enum PrsmCallStatus { PRSM_CALL_OK = 0, PRSM_CALL_FAILED = 1 } PrsmCallStatus;
typedef enum PrsmOperation {
    PRSM_OP_CONSTRUCT = 0, PRSM_OP_INITIALIZED, PRSM_OP_ADDED_TO_SCENE,
    PRSM_OP_ENABLE, PRSM_OP_BEGIN_SIMULATION, PRSM_OP_PRE_PHYSICS,
    PRSM_OP_POST_PHYSICS, PRSM_OP_SIMULATE_START, PRSM_OP_END_SIMULATION,
    PRSM_OP_DISABLE, PRSM_OP_REMOVING_FROM_SCENE, PRSM_OP_UNINITIALIZING
} PrsmOperation;
typedef PrsmCallStatus (*PrsmCheckedInvokeFn)(void* self, PrsmOperation operation,
                                             float dt, void** out_frame);
typedef void (*PrsmTraceFn)(const void* object, void* tracer);

struct PrsmTypeDesc {
    CeStr        name;            /* 정규 이름 "Game::AI::Door" */
    CeStr        base_name;       /* 엔진 기반 타입 "Component" — 일반 클래스는 len 0 */
    uint32_t     size;
    uint32_t     align;

    /* GC 추적 비트맵: 8바이트 슬롯 i가 GC 참조면 비트 i가 1.
     * ref_bitmap_words = ceil(size / 8 / 64). 호스트가 공용 trace 함수로 순회한다. */
    const uint64_t* ref_bitmap;
    uint32_t     ref_bitmap_words;

    uint32_t     hook_mask;       /* PRSM_HOOK_* — 구현된 contract fn만 */

    /* 필드 기본값 초기화. 컴포넌트는 init을 선언할 수 없으므로 이것뿐이다.
     * 메모리는 호스트가 0으로 채워 넘긴다. */
    PrsmHookFn   construct;

    PrsmHookFn   phase[PRSM_PHASE_COUNT]; /* 마스크에 없는 칸은 NULL */
    PrsmHookFn   on_enable;
    PrsmHookFn   on_disable;

    PrsmTickFn   pre_physics;     /* 타입별 일괄 */
    PrsmTickFn   post_physics;

    /* OnSimulate 시작: 새 코루틴 프레임을 만들어 돌려준다(GC 객체).
     * 첫 재개는 호스트가 prsm_task_resume으로 한다. */
    void*      (*on_simulate_start)(void* self);

    const PrsmFieldDesc* fields;
    uint32_t     field_count;
    uint32_t     reserved;

    /* Supplements the unconditional bitmap: inspect tags before tracing unions,
     * nullable payloads and heap strings. Never mark conditional slots in bitmap. */
    PrsmTraceFn trace;
    /* If present, replaces ALL legacy lifecycle callbacks for this type. */
    PrsmCheckedInvokeFn checked_invoke;
};

/* ────────────────────────────── 모듈 표 ────────────────────────────── */

typedef struct PrsmModule {
    uint32_t     abi_version;           /* PRSM_ABI_VERSION */
    uint32_t     flags;
    uint8_t      engine_fingerprint[32];/* 컴파일 당시 엔진 인터페이스 파일 지문(SHA-256) */
    uint8_t      project_fingerprint[32];/* 이 프로젝트 export 선언 지문 */
    CeStr        project_name;

    const PrsmTypeDesc* const* types;
    uint32_t     type_count;
    uint32_t     reserved;

    /* 코루틴 구동 */
    PrsmTaskStatus (*task_resume)(void* frame, PrsmWait* out_wait);
    PrsmTaskStatus (*task_cancel)(void* frame);   /* defer를 역순 실행 */

    /* 지연 초기화 val 등 모듈 전역 루트를 추적한다(collect_step이 호출). */
    void (*trace_globals)(void* tracer);

    /* 핫 리로드: 언로드 직전 호출. 이후 이 모듈의 함수 포인터는 무효. */
    void (*on_unload)(void);
} PrsmModule;

/* 스크립트 DLL이 export 하는 유일한 진입점 */
typedef const PrsmModule* (*PrsmModuleGetFn)(void);
#define PRSM_MODULE_GET_SYMBOL "prsm_module_get"

/* ────────────────────────────── 호스트 API (ce_*) ────────────────────────────── */
/* 엔진(하네스)이 export, 스크립트가 직접 import 해 링크한다. */

/* GC 할당. desc의 size/align/ref_bitmap을 쓴다. 수집을 일으키지 않는다.
 * 임계치 초과 시 다음 안전 지점에 수집을 요청하고, 한계 초과 시 NULL —
 * 스크립트 런타임은 NULL을 받으면 ce_panic한다. 메모리는 0으로 채워진다. */
CE_API void* ce_gc_alloc(const PrsmTypeDesc* desc);
CE_API void* ce_gc_alloc_bytes(size_t size);  /* 긴 문자열 버퍼(참조 없음) */

/* 힙 참조 저장 + 삽입 배리어. 코루틴 프레임 슬롯에는 쓰지 않는다. */
CE_API void  ce_gc_store(void** slot, void* value);

/* 코루틴 중단 시 프레임을 회색으로 되돌린다(재스캔 예약). */
CE_API void  ce_gc_regray(void* frame);

/* 엔진이 스크립트 값을 보관해야 할 때(@escaping 등) 쓰는 루트 */
typedef struct CeRoot { void* opaque; } CeRoot;
CE_API CeRoot ce_gc_root_create(void* object);
CE_API void   ce_gc_root_destroy(CeRoot root);

/* Report one reference from trace_globals or a type trace callback only. */
CE_API void  ce_gc_trace_ref(void* tracer, void* object);

/* 핸들 */
CE_API int   ce_handle_is_alive(CeHandle h);
CE_API void* ce_handle_script_instance(CeHandle h); /* 스크립트 컴포넌트 인스턴스, 아니면 NULL */

/* Legacy native panic: fatal. Checked callbacks must instead propagate
 * ce_report_failure / PRSM_TASK_PANICKED for component-level isolation. */
PRSM_NORETURN CE_API void ce_panic(CeStr message, CeStr file, uint32_t line);

CE_API PrsmCallStatus ce_report_failure(CeStr message, CeStr file, uint32_t line);

CE_API void  ce_log(uint32_t level, CeStr message);

/* ────────────────────────────── 레이아웃 검사 ────────────────────────────── */
#ifdef __cplusplus
}  /* extern "C" */
static_assert(sizeof(CeHandle) == 8,  "CeHandle");
static_assert(sizeof(CeStr)    == 16, "CeStr");
static_assert(sizeof(CeString) == 16, "CeString");
#endif

#endif /* PRSM_ABI_H */
