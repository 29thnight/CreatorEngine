#pragma once
#include "prsm_abi.h"

namespace prsm::host {
// Object-specific restore privilege for prsm_host_reload_validate_restore.
// Only a trusted generation coordinator creates it, around one candidate
// object's state write on the game thread. Nested privileges restore the
// previous object. Ordinary script or native writes never hold it.
class CE_API RestorePrivilege {
public:
    explicit RestorePrivilege(void* object) noexcept;
    ~RestorePrivilege();
    RestorePrivilege(const RestorePrivilege&) = delete;
    RestorePrivilege& operator=(const RestorePrivilege&) = delete;

private:
    void* previous_;
};
}
