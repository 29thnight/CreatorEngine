#include "../Engine/RenderEngine/RenderAllocator.h"
#pragma once

#include "RHI/IDisplayPresentationSink.h"

#include <Windows.h>
#include "../Engine/Utility_Framework/StandardLibrary.h"

namespace Player
{
    // One game-only owner of acquire, composition, pacing, resize and native Present.
    // The renderer only publishes completed images. A future frame-generation owner
    // replaces this implementation and retains every input through its final GPU use;
    // Present returning is never the consumer-complete signal.
    class Presentation;
    template<class T>
    own::shared_owner<Presentation> CreateRetiringPresentation();
    struct PresentationRetirement
    {
        Presentation* m_pointer{};
    };
    class Presentation : public IDisplayPresentationSink
    {
      public:
        virtual bool Initialize(HWND window, uint32_t width, uint32_t height, std::string& outError) = 0;
        virtual bool Resize(uint32_t width, uint32_t height, std::string& outError) = 0;
        // Success may defer an occluded/minimized frame. Present is then a no-op.
        virtual bool BeginFrame(std::string& outError) = 0;
        virtual bool Present(uint64_t textureId, std::string& outError) = 0;
        virtual void Shutdown() = 0;
        // GT calls this before input sampling. A serial SDK owner may wait for
        // the preceding real frame; it never derives markers from generated IDs.
        virtual bool BeginSimulationFrame(uint64_t, std::string&) { return true; }
        virtual void StopSimulationFrames() {}

        bool HasShutdownFailure() const { return m_shutdownFailed; }

        // Counts submitted game compositions followed by a successful native Present call.
        // DXGI_STATUS_OCCLUDED still counts the draw (including the hidden 64x64 smoke),
        // not compositor acceptance or scan-out. Shutdown must separately prove GPU completion.
        // Deferred acquisition and producer-only work never advance this count.
        uint64_t GetSubmittedGameFrames() const { return m_submittedGameFrames.load(std::memory_order_acquire); }

        // Installed before RT starts, cleared only after RT joins. The callback only
        // wakes the existing PT; it never records GPU work on the producer thread.
        void SetDisplayAvailableCallback(std::function<void()> callback) { m_displayAvailable = std::move(callback); }

        void NotifyDisplayAvailable() final
        {
            if (m_displayAvailable)
            {
                m_displayAvailable();
            }
        }

      protected:
        void MarkShutdownFailure() { m_shutdownFailed = true; }

        void RecordSubmittedGameFrame() { m_submittedGameFrames.fetch_add(1, std::memory_order_release); }

      private:
        template<class T>
        friend own::shared_owner<Presentation> CreateRetiringPresentation();
        void SetRetirementContext(own::shared_owner<PresentationRetirement> value) { m_retirement = std::move(value); }
        own::shared_owner<PresentationRetirement> m_retirement;
        std::atomic<uint64_t> m_submittedGameFrames{0};
        bool m_shutdownFailed{false};
        std::function<void()> m_displayAvailable;
    };

    template<class T>
    own::shared_owner<Presentation> CreateRetiringPresentation()
    {
        auto context = own::make_shared<PresentationRetirement>();
        const own::retirement_hook retirement{context.operator->(), [](void* opaque, own::retirement_task task) noexcept
                                              {
                                                  auto* presentation = static_cast<T*>(
                                                      static_cast<PresentationRetirement*>(opaque)->m_pointer);
                                                  presentation->Shutdown();
                                                  if (presentation->IsQuarantined())
                                                  {
                                                      // Preserve the existing failed-GPU-shutdown quarantine, including
                                                      // the actual ownership control block and native payload.
                                                      (void)new own::retirement_task(std::move(task));
                                                  }
                                                  else
                                                  {
                                                      task.run();
                                                  }
                                              }};
        auto value = own::make_shared_with<T>(retirement);
        context->m_pointer = value.operator->();
        value->SetRetirementContext(std::move(context));
        return value;
    }

    own::shared_owner<Presentation> CreateDX12Presentation();
    own::shared_owner<Presentation> CreateVulkanPresentation();
} // namespace Player
