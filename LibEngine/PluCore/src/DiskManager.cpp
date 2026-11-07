//
// Created by Plutex on 1/6/26.
//

#include "PluEngine/Core/DiskManager.h"
#include <fstream>
#include <string>
#include <locale>
#include <cerrno>
#include <cstring>
#include <filesystem>

#include "PluBase/Log.h"

namespace Plu
{
	// 256 KB stdio buffer — keeps bulk sequential writes/reads disk-bound.
	static constexpr UInt64 kBinaryIOBufferSize = 256 * 1024;

	// =========================================================================
	// Failure diagnostics — shared by BinaryFileWriter/BinaryFileReader
	// =========================================================================

	// errno → "ENOENT (No such file or directory)". Falls back to the raw number
	// for codes we do not name explicitly.
	static String DescribeErrno(int err) noexcept
	{
		const char* name = nullptr;
		switch (err)
		{
			case 0:				name = "no error reported"; break;
			case ENOENT:		name = "ENOENT"; break;
			case EACCES:		name = "EACCES"; break;
			case EPERM:			name = "EPERM"; break;
			case EISDIR:		name = "EISDIR"; break;
			case ENOTDIR:		name = "ENOTDIR"; break;
			case ENOSPC:		name = "ENOSPC"; break;
			case EROFS:			name = "EROFS"; break;
			case EMFILE:		name = "EMFILE"; break;
			case ENFILE:		name = "ENFILE"; break;
			case ENAMETOOLONG:	name = "ENAMETOOLONG"; break;
			case EBUSY:			name = "EBUSY"; break;
			case EIO:			name = "EIO"; break;
			case EFBIG:			name = "EFBIG"; break;
			case ELOOP:			name = "ELOOP"; break;
			default: break;
		}

		const String message = String::FromNarrow(std::strerror(err));
		if (name) return Format("{0} ({1})", name, message);
		return Format("errno {0} ({1})", err, message);
	}

	// Inspects the path itself so the log says *why* the open could not work —
	// missing parent directory, path is a directory, file simply absent, etc.
	// Best-effort only: filesystem queries that themselves fail are skipped.
	static String DiagnosePath(const char* path, bool forWriting) noexcept
	{
		std::error_code ec;
		const std::filesystem::path fsPath(path ? path : "");

		if (fsPath.empty()) return String("path is empty");

		const bool exists = std::filesystem::exists(fsPath, ec);
		if (ec) return String("could not stat path");

		if (exists)
		{
			if (std::filesystem::is_directory(fsPath, ec) && !ec)
				return String("path is a directory, not a file");

			const auto perms = std::filesystem::status(fsPath, ec).permissions();
			if (!ec)
			{
				const bool writable = (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
				const bool readable = (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
				if (forWriting && !writable) return String("file exists but is read-only");
				if (!forWriting && !readable) return String("file exists but is not readable");
			}

			if (!forWriting)
			{
				const auto size = std::filesystem::file_size(fsPath, ec);
				if (!ec && size == 0) return String("file exists but is empty (0 bytes)");
			}
			return String("file exists and looks accessible — the open failed for another reason");
		}

		// Not present: the useful distinction is "no file" vs "no directory at all".
		const std::filesystem::path parent = fsPath.parent_path();
		if (!parent.empty() && !std::filesystem::exists(parent, ec))
			return Format("parent directory does not exist: {0}", String::FromNarrow(parent.string().c_str()));

		return String(forWriting ? "file does not exist yet (would be created)" : "file does not exist");
	}

	// Explains a short read/write: stdio only tells us "fewer bytes than asked",
	// so combine ferror/feof with errno and how far the stream actually got.
	static String DescribeShortTransfer(std::FILE* file, UInt64 requested, UInt64 transferred, int err, bool writing) noexcept
	{
		String reason;
		if (std::ferror(file))
			reason = Format("stream error, {0}", DescribeErrno(err));
		else if (!writing && std::feof(file))
			reason = String("unexpected end of file — the file is truncated or the format does not match");
		else
			reason = Format("stream reported no error, {0}", DescribeErrno(err));

		const long position = std::ftell(file);
		return Format("{0} bytes of {1} {2}; {3} (stream position: {4})",
			transferred, requested, writing ? "written" : "read", reason,
			position >= 0 ? String::FromInt(position) : String("unknown"));
	}

	// =========================================================================
	// BinaryFileWriter
	// =========================================================================
	BinaryFileWriter::BinaryFileWriter(BinaryFileWriter&& other) noexcept
		: mFile(other.mFile), mError(other.mError), mLastError(std::move(other.mLastError))
	{
		other.mFile = nullptr;
		other.mError = false;
		other.mLastError = String();
	}

	BinaryFileWriter& BinaryFileWriter::operator=(BinaryFileWriter&& other) noexcept
	{
		if (this != &other)
		{
			CloseFile();
			mFile = other.mFile;
			mError = other.mError;
			mLastError = std::move(other.mLastError);
			other.mFile = nullptr;
			other.mError = false;
			other.mLastError = String();
		}
		return *this;
	}

	bool BinaryFileWriter::OpenInternal(std::FILE* file, const char* debugName, int openErrno) noexcept
	{
		if (mFile) CloseFile();
		if (!file)
		{
			mLastError = Format("{0} — {1}", DescribeErrno(openErrno), DiagnosePath(debugName, true));
			PLU_CORE_ERROR("Failed to open file for writing: {0} — {1}", debugName, mLastError.CStr());
			mError = true;
			return false;
		}
		mFile = file;
		mError = false;
		mLastError = String();
		std::setvbuf(mFile, nullptr, _IOFBF, kBinaryIOBufferSize);
		return true;
	}

	bool BinaryFileWriter::OpenFile(const Path& path) noexcept
	{
		errno = 0;
		std::FILE* file = std::fopen(path.CStr(), "wb");
		const int openErrno = errno;
		return OpenInternal(file, path.CStr(), openErrno);
	}

	bool BinaryFileWriter::OpenFile(const PathW& path) noexcept
	{
#ifdef PLU_PLATFORM_WINDOWS
		std::FILE* file = nullptr;
		errno = 0;
		_wfopen_s(&file, path.CStr(), L"wb");
		const int openErrno = errno;
		const String narrow = String::FromWide(path.CStr());
		return OpenInternal(file, narrow.CStr(), openErrno);
#else
		const String narrow = String::FromWide(path.CStr());
		errno = 0;
		std::FILE* file = std::fopen(narrow.CStr(), "wb");
		const int openErrno = errno;
		return OpenInternal(file, narrow.CStr(), openErrno);
#endif
	}

	bool BinaryFileWriter::Write(const void* data, UInt64 size) noexcept
	{
		if (!mFile || mError) return false;
		if (size == 0) return true;
		errno = 0;
		const UInt64 written = std::fwrite(data, 1, size, mFile);
		if (written != size)
		{
			mLastError = DescribeShortTransfer(mFile, size, written, errno, true);
			PLU_CORE_ERROR("BinaryFileWriter: short write — {0}", mLastError.CStr());
			mError = true;
			return false;
		}
		return true;
	}

	bool BinaryFileWriter::WriteString(const String& str) noexcept
	{
		const UInt32 length = static_cast<UInt32>(str.Length());
		if (!Write(length)) return false;
		return Write(str.CStr(), length);
	}

	bool BinaryFileWriter::CloseFile() noexcept
	{
		if (!mFile) return false;
		errno = 0;
		// fclose flushes the 256 KB buffer, so a full disk usually surfaces here,
		// not at the Write() that produced the data.
		const bool closed = std::fclose(mFile) == 0;
		const int closeErrno = errno;
		if (!closed)
		{
			mLastError = Format("flush/close failed — {0}", DescribeErrno(closeErrno));
			PLU_CORE_ERROR("Error closing file after binary write: {0}", mLastError.CStr());
			mError = true;
		}
		mFile = nullptr;
		return closed && !mError;
	}

	// =========================================================================
	// BinaryFileReader
	// =========================================================================
	BinaryFileReader::BinaryFileReader(BinaryFileReader&& other) noexcept
		: mFile(other.mFile), mError(other.mError), mLastError(std::move(other.mLastError))
	{
		other.mFile = nullptr;
		other.mError = false;
		other.mLastError = String();
	}

	BinaryFileReader& BinaryFileReader::operator=(BinaryFileReader&& other) noexcept
	{
		if (this != &other)
		{
			CloseFile();
			mFile = other.mFile;
			mError = other.mError;
			mLastError = std::move(other.mLastError);
			other.mFile = nullptr;
			other.mError = false;
			other.mLastError = String();
		}
		return *this;
	}

	bool BinaryFileReader::OpenInternal(std::FILE* file, const char* debugName, int openErrno) noexcept
	{
		if (mFile) CloseFile();
		if (!file)
		{
			mLastError = Format("{0} — {1}", DescribeErrno(openErrno), DiagnosePath(debugName, false));
			PLU_CORE_ERROR("Failed to open file for reading: {0} — {1}", debugName, mLastError.CStr());
			mError = true;
			return false;
		}
		mFile = file;
		mError = false;
		mLastError = String();
		std::setvbuf(mFile, nullptr, _IOFBF, kBinaryIOBufferSize);
		return true;
	}

	bool BinaryFileReader::OpenFile(const Path& path) noexcept
	{
		errno = 0;
		std::FILE* file = std::fopen(path.CStr(), "rb");
		const int openErrno = errno;
		return OpenInternal(file, path.CStr(), openErrno);
	}

	bool BinaryFileReader::OpenFile(const PathW& path) noexcept
	{
#ifdef _WIN32
		std::FILE* file = nullptr;
		errno = 0;
		_wfopen_s(&file, path.CStr(), L"rb");
		const int openErrno = errno;
		const String narrow = String::FromWide(path.CStr());
		return OpenInternal(file, narrow.CStr(), openErrno);
#else
		const String narrow = String::FromWide(path.CStr());
		errno = 0;
		std::FILE* file = std::fopen(narrow.CStr(), "rb");
		const int openErrno = errno;
		return OpenInternal(file, narrow.CStr(), openErrno);
#endif
	}

	bool BinaryFileReader::Read(void* data, UInt64 size) noexcept
	{
		if (!mFile || mError) return false;
		if (size == 0) return true;
		errno = 0;
		const UInt64 read = std::fread(data, 1, size, mFile);
		if (read != size)
		{
			mLastError = DescribeShortTransfer(mFile, size, read, errno, false);
			PLU_CORE_ERROR("BinaryFileReader: short read — {0}", mLastError.CStr());
			mError = true;
			return false;
		}
		return true;
	}

	bool BinaryFileReader::ReadString(String& outStr) noexcept
	{
		UInt32 length = 0;
		if (!Read(length)) return false;
		if (length == 0)
		{
			outStr = String();
			return true;
		}

		// A garbage length prefix (wrong version, misaligned stream) would otherwise
		// allocate gigabytes before failing — check it against what is left first.
		const long position = std::ftell(mFile);
		if (position >= 0 && std::fseek(mFile, 0, SEEK_END) == 0)
		{
			const long fileSize = std::ftell(mFile);
			std::fseek(mFile, position, SEEK_SET);
			if (fileSize >= 0 && static_cast<UInt64>(length) > static_cast<UInt64>(fileSize - position))
			{
				mLastError = Format("string length prefix is {0} bytes but only {1} bytes remain in the file "
					"(offset {2} of {3}) — the stream is misaligned or the file format version does not match",
					length, fileSize - position, position, fileSize);
				PLU_CORE_ERROR("BinaryFileReader::ReadString: {0}", mLastError.CStr());
				mError = true;
				return false;
			}
		}

		DynamicArray<char> buffer;
		buffer.Resize(length);
		if (!Read(buffer.Data(), length)) return false;
		outStr = String(buffer.Data(), length);
		return true;
	}

	bool BinaryFileReader::CloseFile() noexcept
	{
		if (!mFile) return false;
		const bool ok = std::fclose(mFile) == 0 && !mError;
		mFile = nullptr;
		return ok;
	}

#ifdef PLU_PLATFORM_LINUX
	bool SaveJsonInternal(const String &path, const nlohmann::json &json)
	{
		std::ofstream out(path.CStr());
		try {
			out << json.dump(4);
		} catch (...) {
			PLU_CORE_ERROR("Error saving JSON!");
			return false;
		}
		try {
			out.close();
		} catch (...) {
			PLU_CORE_ERROR("Error closing file after JSON saving!");
			return false;
		}
		return true;
	}
#elif defined(PLU_PLATFORM_WINDOWS)
	bool SaveJsonInternal(const StringW &path, const nlohmann::json &json)
	{
		std::ofstream out(path.CStr());
		try
		{
			out << json.dump(4);
		} catch (...)
		{
			PLU_CORE_ERROR("Error saving JSON!");
			return false;
		}
		try
		{
			out.close();
		} catch (...)
		{
			PLU_CORE_ERROR("Error closing file after JSON saving!");
			return false;
		}
		return true;
	}
#endif

	bool DiskManager::SaveJson(const StringW &path, const nlohmann::json &json)
	{
#ifdef PLU_PLATFORM_LINUX
		const String narrow = String::FromWide(path.CStr());
		return SaveJsonInternal(narrow, json);
#elif defined(PLU_PLATFORM_WINDOWS)
		return SaveJsonInternal(path, json);
#endif
	}

	bool DiskManager::SaveText(const StringW &path, const String &text)
	{
#ifdef PLU_PLATFORM_LINUX
		std::ofstream out(String::FromWide(path.CStr()).CStr());
#elif defined(PLU_PLATFORM_WINDOWS)
		std::ofstream out(path.CStr());
#endif
		if (!out.is_open()) {
			PLU_CORE_ERROR("Error opening file for text saving!");
			return false;
		}
		try {
			out.write(text.CStr(), static_cast<std::streamsize>(text.Length()));
			out.close();
		} catch (...) {
			PLU_CORE_ERROR("Error saving text file!");
			return false;
		}
		return !out.fail();
	}

	std::optional<nlohmann::json> DiskManager::LoadJson(const PathW &path)
	{
		try {
			nlohmann::json json;
#ifdef PLU_PLATFORM_LINUX
			std::ifstream in(String::FromWide(path.CStr()).CStr());
			json = nlohmann::json::parse(in);
#elif defined(PLU_PLATFORM_WINDOWS)
			std::ifstream in(path.CStr());
			json = nlohmann::json::parse(in);
#endif
			return json;
		} catch (...) {
			PLU_CORE_ERROR("Error loading JSON at: {}", String::FromWide(path.CStr()).CStr());
			return std::nullopt;
		}
	}

	DiskManager::DiskManager()
	{
	}

	DiskManager::~DiskManager()
	{
	}
}
