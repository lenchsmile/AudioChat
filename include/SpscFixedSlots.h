#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include <cstring> // std::memcpy
#include <cassert>

template<typename T>
class SpscFixedSlots
{
public:

	SpscFixedSlots(size_t slotCount,size_t elemsPerSlot)
		: slotCount_(slotCount + 1), // +1 to distinguish full vs empty
		elemsPerSlot_(elemsPerSlot),
		buffer_(slotCount_* elemsPerSlot_),
		writeIndex_(0),
		readIndex_(0)
	{
		assert(slotCount > 1);
	}

	// Try to acquire pointer to next writable slot.
	// Returns nullptr if buffer full.
	T* tryAcquireWrite() noexcept
	{
		size_t w = writeIndex_.load(std::memory_order_relaxed);
		size_t r = readIndex_.load(std::memory_order_acquire);
		size_t next = increment(w);
		if(next == r) return nullptr; // full
		return &buffer_[w * elemsPerSlot_];
	}

	// Commit the previously acquired write slot.
	void commitWrite() noexcept
	{
		size_t w = writeIndex_.load(std::memory_order_relaxed);
		writeIndex_.store(increment(w),std::memory_order_release);
	}

	// Try to acquire pointer to next readable slot.
	// Returns nullptr if buffer empty.
	T* tryAcquireRead() noexcept
	{
		size_t r = readIndex_.load(std::memory_order_relaxed);
		size_t w = writeIndex_.load(std::memory_order_acquire);
		if(r == w) return nullptr; // empty
		return &buffer_[r * elemsPerSlot_];
	}

	// Commit the previously acquired read slot.
	void commitRead() noexcept
	{
		size_t r = readIndex_.load(std::memory_order_relaxed);
		readIndex_.store(increment(r),std::memory_order_release);
	}

	// Helper to copy into a writable slot in one call. Returns true if written.
	bool tryWriteFrom(const T* src) noexcept
	{
		T* dst = tryAcquireWrite();
		if(!dst) return false;
		std::memcpy(dst,src,elemsPerSlot_ * sizeof(T));
		commitWrite();
		return true;
	}

	// Helper to copy out of a readable slot in one call. Returns true if read.
	bool tryReadTo(T* dst) noexcept
	{
		T* src = tryAcquireRead();
		if(!src) return false;
		std::memcpy(dst,src,elemsPerSlot_ * sizeof(T));
		commitRead();
		return true;
	}

	size_t capacitySlots() const noexcept
	{
		return slotCount_ - 1;
	}
	size_t elemsPerSlot() const noexcept
	{
		return elemsPerSlot_;
	}

private:
	size_t increment(size_t idx) const noexcept
	{
		return (idx + 1) % slotCount_;
	}

	const size_t slotCount_;
	const size_t elemsPerSlot_;
	std::vector<T> buffer_;
	std::atomic<size_t> writeIndex_;
	std::atomic<size_t> readIndex_;
};