/*
 * prsm_jit.h — PrSM embedded ORC JIT C ABI, version 1
 *
 * 편집기 같은 임베딩 호스트가 prsmc emit-module-ir 출력(최적화·검증된 컴포넌트
 * 모듈 IR)을 같은 프로세스에서 실행할 때 쓰는 계약이다. prsm_abi.h의 모듈 표를
 * 대체하지 않는다. 이 라이브러리는 모듈 IR을 즉시 구체화해 prsm_module_get 주소를
 * 돌려줄 뿐이고, GC 등록·생명주기·리로드는 호스트 런타임이 맡는다.
 *
 * 규칙
 *   - 모듈 IR이 참조하는 외부 함수·전역은 전부 symbols 목록에 있어야 한다.
 *     없으면 코드를 제출하기 전에 PRSM_JIT_REJECTED로 거부한다(D-047).
 *     호스트 심볼은 절대 주소로 정의되어 같은 이름의 프로세스 export보다 먼저 쓰인다.
 *   - 성공한 load는 prsm_module_get과 그 의존 코드를 전부 구체화한 뒤 돌아온다.
 *     지연 JIT은 v1 범위가 아니다.
 *   - 모듈 표·콜백·GC 서술자는 prsm_jit_unload_module 전까지만 유효하다.
 *     호스트는 GC가 그 모듈 객체를 전부 회수(DrainModule)한 뒤에만 내린다.
 *   - 함수는 패닉이나 예외를 경계 밖으로 내보내지 않는다. 실패는 상태 코드와
 *     NUL 종료 오류 문자열(잘릴 수 있음)로 보고한다.
 *   - 한 모듈 핸들을 여러 스레드가 동시에 쓰지 않는다.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef PRSM_JIT_H
#define PRSM_JIT_H

#include <stddef.h>
#include <stdint.h>

#include "prsm_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRSM_JIT_API_VERSION 1u

typedef struct PrsmJitModule PrsmJitModule;

/* 호스트가 허가한 네이티브 심볼 하나. name은 NUL 종료 UTF-8, address는 비NULL. */
typedef struct PrsmJitSymbol {
    const char* name;
    const void* address;
} PrsmJitSymbol;

typedef enum PrsmJitStatus {
    PRSM_JIT_OK               = 0,
    PRSM_JIT_INVALID_ARGUMENT = 1, /* NULL·잘못된 UTF-8·중복/빈 심볼 */
    PRSM_JIT_REJECTED         = 2, /* 허가되지 않은 외부 심볼 참조 */
    PRSM_JIT_LLVM_FAILED      = 3, /* 파싱·대상 불일치·검증·구체화 실패 */
    PRSM_JIT_INTERNAL_FAILURE = 4  /* 라이브러리 내부 결함(패닉 포착) */
} PrsmJitStatus;

/* 함수 포인터 형식은 LoadLibrary/GetProcAddress 소비자용, 선언은 import 링크용이다.
 * 이 라이브러리가 구현한 PRSM_JIT_API_VERSION. 호스트는 먼저 일치를 확인한다. */
typedef uint32_t (*PrsmJitApiVersionFn)(void);
/* 링크된 LLVM과 고정 백엔드 설정. 캐시 키·진단용. buffer에 NUL 종료로 복사하고
 * 필요한 바이트 수(NUL 제외)를 돌려준다. */
typedef size_t (*PrsmJitBackendIdentityFn)(char* buffer, size_t capacity);
typedef int32_t (*PrsmJitLoadModuleFn)(const char* ir, size_t ir_length,
                                       const PrsmJitSymbol* symbols, uint32_t symbol_count,
                                       PrsmJitModule** out_module,
                                       char* error, size_t error_capacity);
typedef PrsmModuleGetFn (*PrsmJitModuleGetFn)(const PrsmJitModule* module);
typedef void (*PrsmJitUnloadModuleFn)(PrsmJitModule* module);

PRSM_IMPORT uint32_t prsm_jit_api_version(void);
PRSM_IMPORT size_t prsm_jit_backend_identity(char* buffer, size_t capacity);
PRSM_IMPORT int32_t prsm_jit_load_module(const char* ir, size_t ir_length,
                                         const PrsmJitSymbol* symbols, uint32_t symbol_count,
                                         PrsmJitModule** out_module,
                                         char* error, size_t error_capacity);
PRSM_IMPORT PrsmModuleGetFn prsm_jit_module_get(const PrsmJitModule* module);
PRSM_IMPORT void prsm_jit_unload_module(PrsmJitModule* module);

#ifdef __cplusplus
}
#endif

#endif /* PRSM_JIT_H */
