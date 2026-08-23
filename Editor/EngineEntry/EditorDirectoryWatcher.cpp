#include "EditorDirectoryWatcher.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
	constexpr DWORD WatchBufferSize = 63u * 1024u;
	constexpr auto RenamePairTimeout = std::chrono::milliseconds(250);
	constexpr DWORD ExtendedNotificationClass = 2;

	// FILE_NOTIFY_EXTENDED_INFORMATION is not declared by every Windows SDK that
	// can build CreatorEngine. Keep the ABI-local definition beside the dynamic
	// ReadDirectoryChangesExW lookup instead of raising the repository SDK floor.
	struct ExtendedNotification
	{
		DWORD nextEntryOffset;
		DWORD action;
		LARGE_INTEGER creationTime;
		LARGE_INTEGER lastModificationTime;
		LARGE_INTEGER lastChangeTime;
		LARGE_INTEGER lastAccessTime;
		LARGE_INTEGER allocatedLength;
		LARGE_INTEGER fileSize;
		DWORD fileAttributes;
		DWORD reparsePointTag;
		LARGE_INTEGER fileId;
		LARGE_INTEGER parentFileId;
		DWORD fileNameLength;
		WCHAR fileName[1];
	};

	using ReadDirectoryChangesExFunction = BOOL(WINAPI*)(
		HANDLE directory,
		void* buffer,
		DWORD bufferLength,
		BOOL watchSubtree,
		DWORD notifyFilter,
		DWORD* bytesReturned,
		OVERLAPPED* overlapped,
		LPOVERLAPPED_COMPLETION_ROUTINE completionRoutine,
		DWORD informationClass);

	struct NativeNotification
	{
		DWORD action{};
		std::filesystem::path relativePath{};
		std::uint64_t fileId{};
		bool hasFileId{};
	};

	std::error_code WindowsError(DWORD value = ::GetLastError()) noexcept
	{
		return { static_cast<int>(value), std::system_category() };
	}

	bool IsSafeRelativePath(const std::filesystem::path& path)
	{
		if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
		return std::none_of(path.begin(), path.end(),
			[](const std::filesystem::path& part) { return part == L".."; });
	}

	std::optional<std::filesystem::path> NotificationPath(
		const WCHAR* filename, DWORD filenameBytes)
	{
		if (nullptr == filename || 0 == filenameBytes ||
			0 != filenameBytes % sizeof(WCHAR))
		{
			return std::nullopt;
		}

		const std::filesystem::path relative = std::filesystem::path(
			std::wstring(filename, filenameBytes / sizeof(WCHAR))).lexically_normal();
		if (!IsSafeRelativePath(relative)) return std::nullopt;
		return relative;
	}

	template <typename Notification>
	bool RecordFits(const Notification* notification, std::size_t remaining,
		std::size_t filenameOffset, DWORD filenameBytes)
	{
		if (nullptr == notification || 0 != filenameBytes % sizeof(WCHAR)) return false;
		if (filenameOffset > remaining) return false;
		return static_cast<std::size_t>(filenameBytes) <= remaining - filenameOffset;
	}

	bool ParseClassicNotifications(const std::byte* buffer, DWORD byteCount,
		std::vector<NativeNotification>& notifications)
	{
		std::size_t offset = 0;
		while (offset < byteCount)
		{
			const std::size_t remaining = byteCount - offset;
			constexpr std::size_t FilenameOffset =
				offsetof(FILE_NOTIFY_INFORMATION, FileName);
			if (remaining < FilenameOffset) return false;

			const auto* notification = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
				buffer + offset);
			if (!RecordFits(notification, remaining, FilenameOffset,
				notification->FileNameLength))
			{
				return false;
			}

			const auto path = NotificationPath(
				notification->FileName, notification->FileNameLength);
			if (!path) return false;
			notifications.push_back({ notification->Action, *path, 0, false });

			if (0 == notification->NextEntryOffset) return true;
			const std::size_t next = notification->NextEntryOffset;
			const std::size_t recordBytes = FilenameOffset +
				notification->FileNameLength;
			if (0 != next % alignof(DWORD) || next < recordBytes || next >= remaining)
			{
				return false;
			}
			offset += next;
		}
		return offset == byteCount;
	}

	bool ParseExtendedNotifications(const std::byte* buffer, DWORD byteCount,
		std::vector<NativeNotification>& notifications)
	{
		std::size_t offset = 0;
		while (offset < byteCount)
		{
			const std::size_t remaining = byteCount - offset;
			constexpr std::size_t FilenameOffset =
				offsetof(ExtendedNotification, fileName);
			if (remaining < FilenameOffset) return false;

			const auto* notification = reinterpret_cast<const ExtendedNotification*>(
				buffer + offset);
			if (!RecordFits(notification, remaining, FilenameOffset,
				notification->fileNameLength))
			{
				return false;
			}

			const auto path = NotificationPath(
				notification->fileName, notification->fileNameLength);
			if (!path) return false;
			notifications.push_back({ notification->action, *path,
				static_cast<std::uint64_t>(notification->fileId.QuadPart), true });

			if (0 == notification->nextEntryOffset) return true;
			const std::size_t next = notification->nextEntryOffset;
			const std::size_t recordBytes = FilenameOffset +
				notification->fileNameLength;
			if (0 != next % alignof(DWORD) || next < recordBytes || next >= remaining)
			{
				return false;
			}
			offset += next;
		}
		return offset == byteCount;
	}
}

struct EditorDirectoryWatcher::Impl final
{
	struct PendingRename
	{
		std::filesystem::path path{};
		std::uint64_t fileId{};
		bool hasFileId{};
		std::chrono::steady_clock::time_point observedAt{};
	};

	~Impl()
	{
		Stop();
	}

	std::error_code Start(const std::filesystem::path& requestedRoot,
		Handler requestedHandler)
	{
		std::lock_guard lock(m_lifecycleMutex);
		if (m_worker.joinable() || m_running.load(std::memory_order_acquire))
			return std::make_error_code(std::errc::device_or_resource_busy);
		if (!requestedHandler || requestedRoot.empty())
			return std::make_error_code(std::errc::invalid_argument);

		std::error_code filesystemError;
		m_root = std::filesystem::absolute(requestedRoot, filesystemError)
			.lexically_normal();
		if (filesystemError) return filesystemError;
		if (!std::filesystem::is_directory(m_root, filesystemError))
		{
			if (filesystemError) return filesystemError;
			return std::make_error_code(std::errc::not_a_directory);
		}

		m_directory = ::CreateFileW(m_root.c_str(), FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			nullptr);
		if (INVALID_HANDLE_VALUE == m_directory)
		{
			const std::error_code error = WindowsError();
			ResetStoppedState();
			return error;
		}

		m_ioEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (nullptr == m_ioEvent)
		{
			const std::error_code error = WindowsError();
			ResetStoppedState();
			return error;
		}
		m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (nullptr == m_stopEvent)
		{
			const std::error_code error = WindowsError();
			ResetStoppedState();
			return error;
		}

		m_buffer.resize(WatchBufferSize);
		m_handler = std::move(requestedHandler);
		m_stopRequested.store(false, std::memory_order_release);
		ResolveExtendedRead();

		if (const std::error_code error = IssueRead())
		{
			ResetStoppedState();
			return error;
		}

		m_running.store(true, std::memory_order_release);
		try
		{
			m_worker = std::thread(&Impl::Run, this);
		}
		catch (const std::system_error& exception)
		{
			m_running.store(false, std::memory_order_release);
			CancelAndDrainRead();
			const std::error_code error = exception.code();
			ResetStoppedState();
			return error;
		}
		return {};
	}

	void Stop() noexcept
	{
		std::lock_guard lock(m_lifecycleMutex);
		m_stopRequested.store(true, std::memory_order_release);
		if (nullptr != m_stopEvent) ::SetEvent(m_stopEvent);
		if (INVALID_HANDLE_VALUE != m_directory)
			::CancelIoEx(m_directory, &m_overlapped);

		// A handler may request a stop, but it cannot join or destroy its own
		// execution thread. The owner must call Stop() again from its host thread.
		if (m_worker.joinable() && m_worker.get_id() == std::this_thread::get_id())
			return;

		if (m_worker.joinable()) m_worker.join();
		m_running.store(false, std::memory_order_release);
		ResetStoppedState();
	}

	bool IsRunning() const noexcept
	{
		return m_running.load(std::memory_order_acquire);
	}

private:
	void ResolveExtendedRead() noexcept
	{
		const HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
		m_readExtended = nullptr == kernel ? nullptr :
			reinterpret_cast<ReadDirectoryChangesExFunction>(
				::GetProcAddress(kernel, "ReadDirectoryChangesExW"));
	}

	std::error_code IssueRead() noexcept
	{
		m_overlapped = {};
		m_overlapped.hEvent = m_ioEvent;
		::ResetEvent(m_ioEvent);

		constexpr DWORD NotifyFilter = FILE_NOTIFY_CHANGE_CREATION |
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
			FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE;

		if (nullptr != m_readExtended)
		{
			if (m_readExtended(m_directory, m_buffer.data(),
				static_cast<DWORD>(m_buffer.size()), TRUE, NotifyFilter, nullptr,
				&m_overlapped, nullptr, ExtendedNotificationClass))
			{
				m_usingExtendedNotifications = true;
				m_readOutstanding = true;
				return {};
			}
			// A filesystem or older Windows build may reject the extended form
			// even when Kernel32 exports it. Keep the classic recursive fallback.
			m_readExtended = nullptr;
			m_overlapped = {};
			m_overlapped.hEvent = m_ioEvent;
			::ResetEvent(m_ioEvent);
		}

		if (!::ReadDirectoryChangesW(m_directory, m_buffer.data(),
			static_cast<DWORD>(m_buffer.size()), TRUE, NotifyFilter, nullptr,
			&m_overlapped, nullptr))
		{
			return WindowsError();
		}

		m_usingExtendedNotifications = false;
		m_readOutstanding = true;
		return {};
	}

	void Run() noexcept
	{
		const HANDLE waits[]{ m_stopEvent, m_ioEvent };
		while (!m_stopRequested.load(std::memory_order_acquire))
		{
			const DWORD waitResult = ::WaitForMultipleObjects(
				static_cast<DWORD>(std::size(waits)), waits, FALSE,
				PendingRenameWaitMilliseconds());

			if (WAIT_OBJECT_0 == waitResult)
			{
				CancelAndDrainRead();
				break;
			}
			if (WAIT_TIMEOUT == waitResult)
			{
				std::vector<Change> expired;
				FlushExpiredRenames(expired);
				Dispatch(expired);
				continue;
			}
			if (WAIT_OBJECT_0 + 1 != waitResult)
			{
				DispatchRescan(WindowsError());
				CancelAndDrainRead();
				break;
			}

			DWORD byteCount = 0;
			const BOOL completed = ::GetOverlappedResult(
				m_directory, &m_overlapped, &byteCount, FALSE);
			m_readOutstanding = false;
			if (!completed)
			{
				const DWORD error = ::GetLastError();
				if (ERROR_OPERATION_ABORTED == error &&
					m_stopRequested.load(std::memory_order_acquire))
				{
					break;
				}

				m_pendingRenames.clear();
				DispatchRescan(WindowsError(error));
				if (m_stopRequested.load(std::memory_order_acquire)) break;
				if (const std::error_code rearmError = IssueRead())
				{
					DispatchRescan(rearmError);
					break;
				}
				continue;
			}

			if (m_stopRequested.load(std::memory_order_acquire)) break;
			if (0 == byteCount)
			{
				m_pendingRenames.clear();
				const std::error_code rearmError = IssueRead();
				DispatchRescan(WindowsError(ERROR_NOTIFY_ENUM_DIR));
				if (rearmError)
				{
					DispatchRescan(rearmError);
					break;
				}
				continue;
			}

			std::vector<NativeNotification> nativeNotifications;
			const bool parsed = m_usingExtendedNotifications
				? ParseExtendedNotifications(
					m_buffer.data(), byteCount, nativeNotifications)
				: ParseClassicNotifications(
					m_buffer.data(), byteCount, nativeNotifications);
			if (!parsed)
			{
				m_pendingRenames.clear();
				const std::error_code rearmError = IssueRead();
				DispatchRescan(WindowsError(ERROR_INVALID_DATA));
				if (rearmError)
				{
					DispatchRescan(rearmError);
					break;
				}
				continue;
			}

			std::vector<Change> changes;
			ProcessNotifications(nativeNotifications, changes);
			FlushExpiredRenames(changes);

			// Re-arm before invoking user code. Meta generation and asset reload may
			// perform file I/O, and those changes must already have a kernel buffer.
			const std::error_code rearmError = IssueRead();
			if (!m_stopRequested.load(std::memory_order_acquire)) Dispatch(changes);
			if (rearmError)
			{
				DispatchRescan(rearmError);
				break;
			}
		}

		m_running.store(false, std::memory_order_release);
	}

	void ProcessNotifications(
		const std::vector<NativeNotification>& nativeNotifications,
		std::vector<Change>& changes)
	{
		for (const NativeNotification& notification : nativeNotifications)
		{
			const std::filesystem::path fullPath =
				(m_root / notification.relativePath).lexically_normal();
			switch (notification.action)
			{
			case FILE_ACTION_ADDED:
				changes.push_back({ ChangeKind::Added, fullPath });
				break;
			case FILE_ACTION_REMOVED:
				changes.push_back({ ChangeKind::Removed, fullPath });
				break;
			case FILE_ACTION_MODIFIED:
				changes.push_back({ ChangeKind::Modified, fullPath });
				break;
			case FILE_ACTION_RENAMED_OLD_NAME:
				RememberOldName(notification, fullPath, changes);
				break;
			case FILE_ACTION_RENAMED_NEW_NAME:
				CompleteRename(notification, fullPath, changes);
				break;
			default:
				break;
			}
		}
	}

	void RememberOldName(const NativeNotification& notification,
		const std::filesystem::path& oldPath, std::vector<Change>& changes)
	{
		if (notification.hasFileId)
		{
			const auto duplicate = std::find_if(m_pendingRenames.begin(),
				m_pendingRenames.end(), [&](const PendingRename& pending)
				{
					return pending.hasFileId && pending.fileId == notification.fileId;
				});
			if (duplicate != m_pendingRenames.end())
			{
				changes.push_back({ ChangeKind::Removed, duplicate->path });
				m_pendingRenames.erase(duplicate);
			}
		}

		m_pendingRenames.push_back({ oldPath, notification.fileId,
			notification.hasFileId, std::chrono::steady_clock::now() });
	}

	void CompleteRename(const NativeNotification& notification,
		const std::filesystem::path& newPath, std::vector<Change>& changes)
	{
		auto oldName = m_pendingRenames.end();
		if (notification.hasFileId)
		{
			oldName = std::find_if(m_pendingRenames.begin(), m_pendingRenames.end(),
				[&](const PendingRename& pending)
				{
					return pending.hasFileId && pending.fileId == notification.fileId;
				});
		}
		else
		{
			oldName = std::find_if(m_pendingRenames.begin(), m_pendingRenames.end(),
				[](const PendingRename& pending) { return !pending.hasFileId; });
		}

		if (oldName == m_pendingRenames.end())
		{
			changes.push_back({ ChangeKind::Added, newPath });
			return;
		}

		changes.push_back({ ChangeKind::Renamed, newPath, oldName->path });
		m_pendingRenames.erase(oldName);
	}

	void FlushExpiredRenames(std::vector<Change>& changes)
	{
		const auto now = std::chrono::steady_clock::now();
		auto pending = m_pendingRenames.begin();
		while (pending != m_pendingRenames.end())
		{
			if (now - pending->observedAt < RenamePairTimeout)
			{
				++pending;
				continue;
			}
			changes.push_back({ ChangeKind::Removed, pending->path });
			pending = m_pendingRenames.erase(pending);
		}
	}

	DWORD PendingRenameWaitMilliseconds() const noexcept
	{
		if (m_pendingRenames.empty()) return INFINITE;

		const auto oldest = std::min_element(m_pendingRenames.begin(),
			m_pendingRenames.end(), [](const PendingRename& left,
				const PendingRename& right)
			{
				return left.observedAt < right.observedAt;
			});
		const auto remaining = RenamePairTimeout -
			(std::chrono::steady_clock::now() - oldest->observedAt);
		if (remaining <= std::chrono::steady_clock::duration::zero()) return 0;

		const auto milliseconds = std::chrono::duration_cast<
			std::chrono::milliseconds>(remaining + std::chrono::milliseconds(1));
		return static_cast<DWORD>(std::min<std::int64_t>(milliseconds.count(),
			static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)() - 1)));
	}

	void Dispatch(const std::vector<Change>& changes) noexcept
	{
		for (const Change& change : changes)
		{
			if (m_stopRequested.load(std::memory_order_acquire)) return;
			try
			{
				m_handler(change);
			}
			catch (...)
			{
				// Preserve the notification thread. The database adapter already owns
				// domain logging and recovery for its file/YAML operations.
			}
		}
	}

	void DispatchRescan(std::error_code error) noexcept
	{
		if (m_stopRequested.load(std::memory_order_acquire)) return;
		const Change change{ ChangeKind::RescanRequired, {}, {}, error };
		try
		{
			m_handler(change);
		}
		catch (...)
		{
		}
	}

	void CancelAndDrainRead() noexcept
	{
		if (!m_readOutstanding || INVALID_HANDLE_VALUE == m_directory) return;
		::CancelIoEx(m_directory, &m_overlapped);
		DWORD ignored = 0;
		::GetOverlappedResult(m_directory, &m_overlapped, &ignored, TRUE);
		m_readOutstanding = false;
	}

	void ResetStoppedState() noexcept
	{
		if (m_readOutstanding) CancelAndDrainRead();
		if (INVALID_HANDLE_VALUE != m_directory)
		{
			::CloseHandle(m_directory);
			m_directory = INVALID_HANDLE_VALUE;
		}
		if (nullptr != m_ioEvent)
		{
			::CloseHandle(m_ioEvent);
			m_ioEvent = nullptr;
		}
		if (nullptr != m_stopEvent)
		{
			::CloseHandle(m_stopEvent);
			m_stopEvent = nullptr;
		}

		m_buffer.clear();
		m_pendingRenames.clear();
		m_handler = {};
		m_root.clear();
		m_readExtended = nullptr;
		m_usingExtendedNotifications = false;
		m_stopRequested.store(false, std::memory_order_release);
	}

	std::mutex m_lifecycleMutex;
	std::filesystem::path m_root{};
	Handler m_handler{};
	std::thread m_worker{};
	std::atomic_bool m_running{ false };
	std::atomic_bool m_stopRequested{ false };
	HANDLE m_directory{ INVALID_HANDLE_VALUE };
	HANDLE m_ioEvent{};
	HANDLE m_stopEvent{};
	OVERLAPPED m_overlapped{};
	std::vector<std::byte> m_buffer{};
	std::vector<PendingRename> m_pendingRenames{};
	ReadDirectoryChangesExFunction m_readExtended{};
	bool m_usingExtendedNotifications{};
	bool m_readOutstanding{};
};

EditorDirectoryWatcher::EditorDirectoryWatcher() : m_impl(std::make_unique<Impl>())
{
}

EditorDirectoryWatcher::~EditorDirectoryWatcher()
{
	Stop();
}

std::error_code EditorDirectoryWatcher::Start(
	const std::filesystem::path& root, Handler handler)
{
	return m_impl->Start(root, std::move(handler));
}

void EditorDirectoryWatcher::Stop() noexcept
{
	m_impl->Stop();
}

bool EditorDirectoryWatcher::IsRunning() const noexcept
{
	return m_impl->IsRunning();
}
