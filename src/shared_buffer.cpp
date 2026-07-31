// Phase 30 (foundational types only): immutable shared-data buffers,
// views, and the request-scoped arena.
//
// See masterai.hpp's Phase 30 block for the public contract and scope note.
// This unit implements the non-template pieces: SharedBuffer, BufferView,
// MappedBufferView (Win32 file-mapping / POSIX mmap backends),
// ChunkReference::materialize(), and RequestArena. FixedSizePool and
// ArenaHandle are templates and stay header-only in masterai.hpp.
#include "masterai.hpp"

#include <cstring>
#include <fstream>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace masterai {

// ---------------------------------------------------------------------
// SharedBuffer
// ---------------------------------------------------------------------

SharedBuffer::SharedBuffer(std::vector<std::uint8_t> bytes)
    : storage_(std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes))) {}

SharedBuffer SharedBuffer::copy_from(const void* data, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    if (size > 0U && data != nullptr) {
        std::memcpy(bytes.data(), data, size);
    }
    return SharedBuffer(std::move(bytes));
}

const std::uint8_t* SharedBuffer::data() const noexcept {
    return storage_ ? storage_->data() : nullptr;
}

std::size_t SharedBuffer::size() const noexcept {
    return storage_ ? storage_->size() : 0U;
}

long SharedBuffer::use_count() const noexcept {
    // shared_ptr::use_count() is a diagnostic-only call per the standard --
    // matches this accessor's documented "diagnostic only" contract.
    return storage_.use_count();
}

// ---------------------------------------------------------------------
// BufferView
// ---------------------------------------------------------------------

BufferView::BufferView(SharedBuffer owner, std::size_t offset, std::size_t length,
                       std::string encoding)
    : owner_(std::move(owner)), encoding_(std::move(encoding)) {
    // Clamp out-of-range windows to empty rather than reading past the
    // owning buffer -- fail safe, matching the class's documented contract.
    const std::size_t owner_size = owner_.size();
    if (offset > owner_size) {
        offset_ = owner_size;
        length_ = 0U;
        return;
    }
    offset_ = offset;
    const std::size_t remaining = owner_size - offset;
    length_ = length < remaining ? length : remaining;
}

const std::uint8_t* BufferView::data() const noexcept {
    const std::uint8_t* base = owner_.data();
    return base != nullptr ? base + offset_ : nullptr;
}

std::string BufferView::to_string() const {
    const std::uint8_t* bytes = data();
    if (bytes == nullptr || length_ == 0U) return std::string();
    return std::string(reinterpret_cast<const char*>(bytes), length_);
}

// ---------------------------------------------------------------------
// ChunkReference
// ---------------------------------------------------------------------

std::string ChunkReference::materialize(const BufferView& segment) const {
    // Fails safe (empty string) rather than reading out of bounds if this
    // reference does not actually fit within the supplied segment view --
    // callers are expected to pass the exact segment segment_key names.
    if (offset > segment.size()) return std::string();
    const std::uint64_t remaining = static_cast<std::uint64_t>(segment.size()) - offset;
    const std::uint64_t take = length < remaining ? length : remaining;
    const std::uint8_t* base = segment.data();
    if (base == nullptr || take == 0U) return std::string();
    return std::string(reinterpret_cast<const char*>(base) + offset,
                       static_cast<std::size_t>(take));
}

// ---------------------------------------------------------------------
// TokenSpan
// ---------------------------------------------------------------------

const std::uint8_t* TokenSpan::data() const noexcept {
    const std::uint8_t* base = owner.data();
    return base != nullptr ? base + offset : nullptr;
}

// ---------------------------------------------------------------------
// MappedBufferView
// ---------------------------------------------------------------------

#if defined(_WIN32)
// Win32 backend: CreateFileW + CreateFileMappingW + MapViewOfFile, all
// closed in reverse order on destruction. Handles are stored as void* here
// (rather than including windows.h in masterai.hpp) so the shared header
// stays platform-header-free, same convention as
// Win32OverlappedFileReader::State in async_storage.cpp.
class MappedBufferView::State {
public:
    State(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length) {
        file_handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("MappedBufferView: failed to open file");
        }

        LARGE_INTEGER file_size{};
        if (!GetFileSizeEx(file_handle_, &file_size)) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("MappedBufferView: failed to query file size");
        }
        const std::uint64_t total_size = static_cast<std::uint64_t>(file_size.QuadPart);
        if (offset > total_size) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("MappedBufferView: offset beyond end of file");
        }
        const std::uint64_t available = total_size - offset;
        const std::uint64_t mapped_length = (length == 0U || length > available)
                                                ? available
                                                : length;

        mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping_handle_ == nullptr) {
            CloseHandle(file_handle_);
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("MappedBufferView: CreateFileMappingW failed");
        }

        // MapViewOfFile requires the offset to be aligned to the system
        // allocation granularity; map from the aligned offset and adjust
        // the returned pointer forward by the residual so callers still see
        // exactly [offset, offset+length).
        SYSTEM_INFO system_info{};
        GetSystemInfo(&system_info);
        const std::uint64_t granularity = system_info.dwAllocationGranularity;
        const std::uint64_t aligned_offset = (offset / granularity) * granularity;
        const std::uint64_t alignment_residual = offset - aligned_offset;
        const std::uint64_t view_length = mapped_length + alignment_residual;

        ULARGE_INTEGER split_offset{};
        split_offset.QuadPart = aligned_offset;
        void* mapped = MapViewOfFile(mapping_handle_, FILE_MAP_READ, split_offset.HighPart,
                                     split_offset.LowPart, static_cast<SIZE_T>(view_length));
        if (mapped == nullptr) {
            CloseHandle(mapping_handle_);
            CloseHandle(file_handle_);
            mapping_handle_ = nullptr;
            file_handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("MappedBufferView: MapViewOfFile failed");
        }

        mapped_base_ = mapped;
        data_ = static_cast<std::uint8_t*>(mapped) + alignment_residual;
        length_ = mapped_length;
    }

    ~State() {
        if (mapped_base_ != nullptr) UnmapViewOfFile(mapped_base_);
        if (mapping_handle_ != nullptr) CloseHandle(mapping_handle_);
        if (file_handle_ != INVALID_HANDLE_VALUE) CloseHandle(file_handle_);
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return static_cast<std::size_t>(length_); }

private:
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE mapping_handle_{nullptr};
    void* mapped_base_{nullptr};
    std::uint8_t* data_{nullptr};
    std::uint64_t length_{0};
};
#else
// POSIX fallback backend: open() + fstat() (to resolve length==0 and
// validate offset) + mmap(MAP_PRIVATE, PROT_READ), munmap()'d in reverse
// order on destruction.
class MappedBufferView::State {
public:
    State(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length) {
        file_descriptor_ = ::open(path.c_str(), O_RDONLY);
        if (file_descriptor_ < 0) {
            throw std::runtime_error("MappedBufferView: failed to open file");
        }

        struct stat file_stat{};
        if (::fstat(file_descriptor_, &file_stat) != 0) {
            ::close(file_descriptor_);
            file_descriptor_ = -1;
            throw std::runtime_error("MappedBufferView: failed to stat file");
        }
        const std::uint64_t total_size = static_cast<std::uint64_t>(file_stat.st_size);
        if (offset > total_size) {
            ::close(file_descriptor_);
            file_descriptor_ = -1;
            throw std::runtime_error("MappedBufferView: offset beyond end of file");
        }
        const std::uint64_t available = total_size - offset;
        const std::uint64_t mapped_length = (length == 0U || length > available)
                                                ? available
                                                : length;

        // mmap() requires the offset to be page-aligned; map from the
        // aligned offset and adjust the returned pointer forward by the
        // residual, same approach as the Win32 backend above.
        const long page_size = ::sysconf(_SC_PAGE_SIZE);
        const std::uint64_t granularity =
            page_size > 0 ? static_cast<std::uint64_t>(page_size) : 4096ULL;
        const std::uint64_t aligned_offset = (offset / granularity) * granularity;
        const std::uint64_t alignment_residual = offset - aligned_offset;
        const std::uint64_t view_length = mapped_length + alignment_residual;

        void* mapped = view_length > 0U
                           ? ::mmap(nullptr, static_cast<std::size_t>(view_length), PROT_READ,
                                    MAP_PRIVATE, file_descriptor_,
                                    static_cast<off_t>(aligned_offset))
                           : nullptr;
        if (view_length > 0U && mapped == MAP_FAILED) {
            ::close(file_descriptor_);
            file_descriptor_ = -1;
            throw std::runtime_error("MappedBufferView: mmap failed");
        }

        mapped_base_ = mapped == MAP_FAILED ? nullptr : mapped;
        mapped_view_length_ = view_length;
        data_ = mapped_base_ != nullptr
                    ? static_cast<std::uint8_t*>(mapped_base_) + alignment_residual
                    : nullptr;
        length_ = mapped_length;
    }

    ~State() {
        if (mapped_base_ != nullptr) {
            ::munmap(mapped_base_, static_cast<std::size_t>(mapped_view_length_));
        }
        if (file_descriptor_ >= 0) ::close(file_descriptor_);
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    const std::uint8_t* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return static_cast<std::size_t>(length_); }

private:
    int file_descriptor_{-1};
    void* mapped_base_{nullptr};
    std::uint64_t mapped_view_length_{0};
    std::uint8_t* data_{nullptr};
    std::uint64_t length_{0};
};
#endif

MappedBufferView::MappedBufferView(const std::filesystem::path& path, std::uint64_t offset,
                                   std::uint64_t length, std::string encoding)
    : state_(std::make_unique<State>(path, offset, length)), encoding_(std::move(encoding)) {}

MappedBufferView::~MappedBufferView() = default;
MappedBufferView::MappedBufferView(MappedBufferView&&) noexcept = default;
MappedBufferView& MappedBufferView::operator=(MappedBufferView&&) noexcept = default;

const std::uint8_t* MappedBufferView::data() const noexcept {
    return state_ ? state_->data() : nullptr;
}

std::size_t MappedBufferView::size() const noexcept {
    return state_ ? state_->size() : 0U;
}

// ---------------------------------------------------------------------
// RequestArena
// ---------------------------------------------------------------------

RequestArena::RequestArena(std::size_t capacity_bytes)
    : storage_(capacity_bytes > 0U ? new std::uint8_t[capacity_bytes] : nullptr),
      capacity_bytes_(capacity_bytes) {
#ifndef NDEBUG
    generation_ = std::make_shared<std::atomic<std::uint64_t>>(1ULL);
#endif
}

RequestArena::~RequestArena() {
#ifndef NDEBUG
    // Mark the independent generation token "dead" so any ArenaHandle
    // issued before destruction fails its valid() check even though it can
    // never touch this (about-to-be-freed) arena object directly -- the
    // token block itself stays alive via shared_ptr as long as any handle
    // still references it.
    if (generation_) {
        generation_->store(0ULL, std::memory_order_release);
    }
#endif
}

void* RequestArena::allocate(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0U) return nullptr;
    // Bump-align used_bytes_ up to `alignment` (must be a power of two, per
    // the documented contract) before carving out `size` bytes.
    const std::size_t aligned_used =
        (used_bytes_ + (alignment - 1U)) & ~(alignment - 1U);
    if (aligned_used > capacity_bytes_ || size > capacity_bytes_ - aligned_used) {
        return nullptr;  // exhausted -- documented nullptr-on-failure policy
    }
    void* result = storage_.get() + aligned_used;
    used_bytes_ = aligned_used + size;
    return result;
}

void* RequestArena::allocate_or_throw(std::size_t size, std::size_t alignment) {
    void* result = allocate(size, alignment);
    if (result == nullptr) throw ArenaExhaustedError();
    return result;
}

void RequestArena::reset() noexcept {
    used_bytes_ = 0U;
#ifndef NDEBUG
    // Bump (never to the 0 "dead" sentinel used by the destructor) so
    // handles issued before this reset() become stale.
    std::uint64_t next = generation_->load(std::memory_order_relaxed) + 1ULL;
    if (next == 0ULL) next = 1ULL;
    generation_->store(next, std::memory_order_release);
#endif
}

}  // namespace masterai
