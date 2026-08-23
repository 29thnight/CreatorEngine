#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>

// Editor-owned recursive directory notification source.
//
// The handler is invoked serially on the watcher's I/O thread. It must not call
// Start(), Stop(), or destroy the watcher. Stop() cancels the outstanding read,
// joins that thread, and guarantees that no handler is running when it returns.
//
// Start() arms the first directory read before returning. A caller that needs an
// initial catalog scan can therefore start the watcher first, scan second, and
// reconcile any notifications that arrived during the scan without a blind gap.
class EditorDirectoryWatcher final
{
public:
	enum class ChangeKind : std::uint8_t
	{
		Added,
		Removed,
		Modified,
		Renamed,
		RescanRequired,
	};

	struct Change
	{
		ChangeKind kind{};
		std::filesystem::path currentPath{};
		std::filesystem::path previousPath{};
		std::error_code error{};
	};

	using Handler = std::function<void(const Change&)>;

	EditorDirectoryWatcher();
	~EditorDirectoryWatcher();

	EditorDirectoryWatcher(const EditorDirectoryWatcher&) = delete;
	EditorDirectoryWatcher& operator=(const EditorDirectoryWatcher&) = delete;
	EditorDirectoryWatcher(EditorDirectoryWatcher&&) = delete;
	EditorDirectoryWatcher& operator=(EditorDirectoryWatcher&&) = delete;

	// Returns an empty error_code after the recursive watch has been armed.
	// A non-empty result leaves the watcher stopped and safe to start again.
	std::error_code Start(const std::filesystem::path& root, Handler handler);
	void Stop() noexcept;
	bool IsRunning() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};
