#pragma once

#include "../RHISoftwareEncoder.h"

class VulkanDeviceResources;

class VulkanSoftwareCommandValidator final : public IRHISoftwareCommandValidator
{
  public:
    explicit VulkanSoftwareCommandValidator(VulkanDeviceResources& resources) : m_resources(resources) {}
    bool Validate(RHISoftwareCommandType type, const void* arguments, size_t bytes,
                  const RHISoftwareEncoderState& state) const override;
    void ApplyRecordedState(RHISoftwareCommandType type, const void* arguments, size_t bytes,
                            RHISoftwareEncoderState& state) const override;

  private:
    template<typename T>
    static const T* Read(const void* value, size_t bytes)
    {
        return value && bytes == sizeof(T) ? static_cast<const T*>(value) : nullptr;
    }
    VulkanDeviceResources& m_resources;
};
