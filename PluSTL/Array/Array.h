#pragma once
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <initializer_list>
#include <cstdlib>
#include "Allocators/Default.h"
#include "Random/Random.h"

namespace Plu
{

template<typename T, typename Allocator = DefaultAllocator<T>>
class DynamicArray {
public:
    using ValueType = T;
    using SizeType = size_t;
    using Iterator = T*;
    using ConstIterator = const T*;
    using AllocatorType = Allocator;

    // Konstruktory z obsługą instancji alokatora
    DynamicArray()
        : mData(nullptr), mSize(0), mCapacity(0), mAllocator(Allocator()) {}

    explicit DynamicArray(const Allocator& alloc)
        : mData(nullptr), mSize(0), mCapacity(0), mAllocator(alloc) {}

    explicit DynamicArray(SizeType capacity, const Allocator& alloc = Allocator())
        : mData(nullptr), mSize(0), mCapacity(0), mAllocator(alloc) {
        Reserve(capacity);
    }

    DynamicArray(std::initializer_list<T> init, const Allocator& alloc = Allocator())
        : mData(nullptr), mSize(0), mCapacity(0), mAllocator(alloc) {
        Reserve(init.size());
        for (const auto& item : init) {
            PushBack(item);
        }
    }

    // Copy constructor - kopiuje również alokator
    DynamicArray(const DynamicArray& other)
        : mData(nullptr), mSize(0), mCapacity(0), mAllocator(other.mAllocator) {
        Reserve(other.mSize);
        for (SizeType i = 0; i < other.mSize; ++i) {
            mAllocator.Construct(&mData[i], other.mData[i]);
        }
        mSize = other.mSize;
    }

    // Move constructor
    DynamicArray(DynamicArray&& other) noexcept
        : mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity),
          mAllocator(std::move(other.mAllocator)) {
        other.mData = nullptr;
        other.mSize = 0;
        other.mCapacity = 0;
    }

    // Destruktor - używa instancji alokatora
    ~DynamicArray() {
        Clear();
        if (mData) {
            mAllocator.Deallocate(mData, mCapacity);
        }
    }

    // Copy assignment
    DynamicArray& operator=(const DynamicArray& other) {
        if (this != &other) {
            Clear();
            if (mData) {
                mAllocator.Deallocate(mData, mCapacity);
                mData = nullptr;
                mCapacity = 0;
            }

            mAllocator = other.mAllocator;
            Reserve(other.mSize);
            for (SizeType i = 0; i < other.mSize; ++i) {
                mAllocator.Construct(&mData[i], other.mData[i]);
            }
            mSize = other.mSize;
        }
        return *this;
    }

    // Move assignment
    DynamicArray& operator=(DynamicArray&& other) noexcept {
        if (this != &other) {
            Clear();
            if (mData) {
                mAllocator.Deallocate(mData, mCapacity);
            }

            mData = other.mData;
            mSize = other.mSize;
            mCapacity = other.mCapacity;
            mAllocator = std::move(other.mAllocator);

            other.mData = nullptr;
            other.mSize = 0;
            other.mCapacity = 0;
        }
        return *this;
    }

    // Dodawanie elementów - używa mAllocator.Construct
    void PushBack(const T& value) {
        if (!GrowForPush()) return;
        mAllocator.Construct(&mData[mSize], value);
        ++mSize;
    }

    void PushBack(T&& value) {
        if (!GrowForPush()) return;
        mAllocator.Construct(&mData[mSize], std::move(value));
        ++mSize;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args) {
        // Unlike PushBack this owes the caller a reference, so there is no element to
        // silently drop. Out of memory here is unrecoverable by definition.
        if (!GrowForPush()) std::abort();
        mAllocator.Construct(&mData[mSize], std::forward<Args>(args)...);
        return mData[mSize++];
    }

    void PopBack() {
        if (mSize > 0) {
            --mSize;
            mAllocator.Destroy(&mData[mSize]);
        }
    }

    // Dostęp do elementów
    T& operator[](SizeType index) { return mData[index]; }
    const T& operator[](SizeType index) const { return mData[index]; }

    T& At(SizeType index) {
        if (index >= mSize) throw std::out_of_range("Index out of range");
        return mData[index];
    }

    const T& At(SizeType index) const {
        if (index >= mSize) throw std::out_of_range("Index out of range");
        return mData[index];
    }

    T& Front() { return mData[0]; }
    const T& Front() const { return mData[0]; }
    T& Back() { return mData[mSize - 1]; }
    const T& Back() const { return mData[mSize - 1]; }
    T* Data() { return mData; }
    const T* Data() const { return mData; }

    // Rozmiar i pojemność
    [[nodiscard]] SizeType Size() const { return mSize; }
    [[nodiscard]] SizeType Capacity() const { return mCapacity; }
    [[nodiscard]] bool IsEmpty() const { return mSize == 0; }

    void Reserve(SizeType newCapacity) {
        if (newCapacity <= mCapacity) return;

        T* newData = mAllocator.Allocate(newCapacity);
        // Allocate is failable (see Allocators/Default.h). Leaving the array at its
        // current capacity is the only safe answer; the previous code walked straight
        // into the null.
        if (!newData) return;

        for (SizeType i = 0; i < mSize; ++i) {
            mAllocator.Construct(&newData[i], std::move(mData[i]));
            mAllocator.Destroy(&mData[i]);
        }

        if (mData) {
            mAllocator.Deallocate(mData, mCapacity);
        }

        mData = newData;
        mCapacity = newCapacity;
    }

    void Resize(SizeType newSize) {
        if (newSize > mCapacity) {
            Reserve(newSize);
            if (newSize > mCapacity) return; // allocation failed; leave the array untouched
        }

        if (newSize > mSize) {
            for (SizeType i = mSize; i < newSize; ++i) {
                mAllocator.Construct(&mData[i]);
            }
        } else {
            for (SizeType i = newSize; i < mSize; ++i) {
                mAllocator.Destroy(&mData[i]);
            }
        }

        mSize = newSize;
    }

    void ShrinkToFit() {
        if (mSize < mCapacity) {
            T* newData = mSize > 0 ? mAllocator.Allocate(mSize) : nullptr;
            if (mSize > 0 && !newData) return;

            for (SizeType i = 0; i < mSize; ++i) {
                mAllocator.Construct(&newData[i], std::move(mData[i]));
                mAllocator.Destroy(&mData[i]);
            }

            if (mData) {
                mAllocator.Deallocate(mData, mCapacity);
            }

            mData = newData;
            mCapacity = mSize;
        }
    }

    void Clear() {
        for (SizeType i = 0; i < mSize; ++i) {
            mAllocator.Destroy(&mData[i]);
        }
        mSize = 0;
    }

    // Iteratory
    Iterator Begin() { return mData; }
    ConstIterator Begin() const { return mData; }
    Iterator End() { return mData + mSize; }
    ConstIterator End() const { return mData + mSize; }

    Iterator begin() { return Begin(); }
    ConstIterator begin() const { return Begin(); }
    Iterator end() { return End(); }
    ConstIterator end() const { return End(); }

    // Utility methods - Wyszukiwanie
    Iterator Find(const T& value) {
        for (SizeType i = 0; i < mSize; ++i) {
            if (mData[i] == value) return &mData[i];
        }
        return End();
    }

    ConstIterator Find(const T& value) const {
        for (SizeType i = 0; i < mSize; ++i) {
            if (mData[i] == value) return &mData[i];
        }
        return End();
    }

    template<typename Predicate>
    Iterator FindIf(Predicate pred) {
        for (SizeType i = 0; i < mSize; ++i) {
            if (pred(mData[i])) return &mData[i];
        }
        return End();
    }

    template<typename Predicate>
    ConstIterator FindIf(Predicate pred) const {
        for (SizeType i = 0; i < mSize; ++i) {
            if (pred(mData[i])) return &mData[i];
        }
        return End();
    }

    bool Contains(const T& value) const {
        return Find(value) != End();
    }

    SizeType IndexOf(const T& value) const {
        for (SizeType i = 0; i < mSize; ++i) {
            if (mData[i] == value) return i;
        }
        return static_cast<SizeType>(-1);
    }

    // Utility methods - Usuwanie
    bool Remove(const T& value) {
        Iterator it = Find(value);
        if (it != End()) {
            RemoveAt(it);
            return true;
        }
        return false;
    }

    template<typename Predicate>
    SizeType RemoveIf(Predicate pred) {
        SizeType removed = 0;
        for (SizeType i = 0; i < mSize;) {
            if (pred(mData[i])) {
                RemoveAt(&mData[i]);
                ++removed;
            } else {
                ++i;
            }
        }
        return removed;
    }

    void RemoveAt(SizeType index) {
        if (index >= mSize) throw std::out_of_range("Index out of range");
        RemoveAt(&mData[index]);
    }

    void RemoveAt(Iterator it) {
        if (it < Begin() || it >= End()) return;

        SizeType index = it - Begin();
        mAllocator.Destroy(&mData[index]);

        for (SizeType i = index; i < mSize - 1; ++i) {
            mAllocator.Construct(&mData[i], std::move(mData[i + 1]));
            mAllocator.Destroy(&mData[i + 1]);
        }

        --mSize;
    }

    void RemoveRange(Iterator first, Iterator last) {
        if (first >= last || first < Begin() || last > End()) return;

        SizeType startIdx = first - Begin();
        SizeType endIdx = last - Begin();
        SizeType count = endIdx - startIdx;

        for (SizeType i = startIdx; i < endIdx; ++i) {
            mAllocator.Destroy(&mData[i]);
        }

        for (SizeType i = startIdx; i < mSize - count; ++i) {
            mAllocator.Construct(&mData[i], std::move(mData[i + count]));
            mAllocator.Destroy(&mData[i + count]);
        }

        mSize -= count;
    }

    // Deprecated spellings. Every other PluSTL container calls its removal
    // operations Remove*, so these forward to RemoveAt / RemoveRange and will go
    // away once nothing names them. (Nothing in the engine does today.)
    [[deprecated("use RemoveAt(Iterator)")]]
    void Erase(Iterator it) { RemoveAt(it); }

    [[deprecated("use RemoveRange(Iterator, Iterator)")]]
    void Erase(Iterator first, Iterator last) { RemoveRange(first, last); }

    // Utility methods - Modyfikacja
    void Insert(Iterator pos, const T& value) {
        SizeType index = pos - Begin();
        if (!GrowForPush()) return;

        for (SizeType i = mSize; i > index; --i) {
            mAllocator.Construct(&mData[i], std::move(mData[i - 1]));
            mAllocator.Destroy(&mData[i - 1]);
        }

        mAllocator.Construct(&mData[index], value);
        ++mSize;
    }

    void Insert(Iterator pos, T&& value) {
        SizeType index = pos - Begin();
        if (!GrowForPush()) return;

        for (SizeType i = mSize; i > index; --i) {
            mAllocator.Construct(&mData[i], std::move(mData[i - 1]));
            mAllocator.Destroy(&mData[i - 1]);
        }

        mAllocator.Construct(&mData[index], std::move(value));
        ++mSize;
    }

    void Reverse() {
        for (SizeType i = 0; i < mSize / 2; ++i) {
            T temp = std::move(mData[i]);
            mData[i] = std::move(mData[mSize - 1 - i]);
            mData[mSize - 1 - i] = std::move(temp);
        }
    }

    template<typename Comparator>
    void Sort(Comparator comp) {
        if (mSize <= 1) return;
        QuickSort(0, mSize - 1, comp);
    }

    void Sort() {
        Sort([](const T& a, const T& b) { return a < b; });
    }

    // Dodaje elementy z innej tablicy DynamicArray
    void Append(const DynamicArray& other) {
        if (other.IsEmpty()) return;

        Reserve(mSize + other.mSize);
        for (const auto& item : other) {
            mAllocator.Construct(&mData[mSize], item);
            ++mSize;
        }
    }

    // Dodaje elementy z innej tablicy DynamicArray (wersja Move)
    void Append(DynamicArray&& other) {
        if (other.IsEmpty()) return;

        Reserve(mSize + other.mSize);
        for (auto& item : other) {
            mAllocator.Construct(&mData[mSize], std::move(item));
            ++mSize;
        }
        // Opcjonalnie czyścimy źródło, choć destruktor 'other' i tak by to zrobił
        other.mSize = 0;
    }

    // Dodaje listę inicjalizacyjną, np. arr.Append({1, 2, 3});
    void Append(std::initializer_list<T> init) {
        if (init.size() == 0) return;

        Reserve(mSize + init.size());
        for (const auto& item : init) {
            mAllocator.Construct(&mData[mSize], item);
            ++mSize;
        }
    }

    // Utility methods - Random draws (PluRandom, thread_local engine)
    // Random index; InvalidIndex when the array is empty.
    [[nodiscard]] SizeType GetRandomIndex() const {
        if (mSize == 0) return InvalidIndex;
        return static_cast<SizeType>(PluRandom::NextIndex(mSize));
    }

    // Random element. Throws on an empty array — use GetRandomItemPtr() when
    // emptiness is an expected case.
    T& GetRandomItem() {
        if (mSize == 0) throw std::out_of_range("GetRandomItem on empty array");
        return mData[PluRandom::NextIndex(mSize)];
    }

    const T& GetRandomItem() const {
        if (mSize == 0) throw std::out_of_range("GetRandomItem on empty array");
        return mData[PluRandom::NextIndex(mSize)];
    }

    // Exception-free variant — nullptr when empty.
    T* GetRandomItemPtr() {
        return mSize == 0 ? nullptr : &mData[PluRandom::NextIndex(mSize)];
    }

    const T* GetRandomItemPtr() const {
        return mSize == 0 ? nullptr : &mData[PluRandom::NextIndex(mSize)];
    }

    // Random element matching the predicate (reservoir sampling — single pass,
    // no allocation). nullptr when nothing matches.
    template<typename Predicate>
    T* GetRandomItemIf(Predicate pred) {
        T* picked = nullptr;
        SizeType matches = 0;
        for (SizeType i = 0; i < mSize; ++i) {
            if (!pred(mData[i])) continue;
            ++matches;
            if (PluRandom::NextIndex(matches) == 0) picked = &mData[i];
        }
        return picked;
    }

    // In-place Fisher-Yates shuffle.
    void Shuffle() {
        for (SizeType i = mSize; i > 1; --i) {
            const SizeType j = static_cast<SizeType>(PluRandom::NextIndex(i));
            SwapItems(i - 1, j);
        }
    }

    // Utility methods - Fast removal (O(1), does NOT preserve order)
    void RemoveAtSwap(SizeType index) {
        if (index >= mSize) throw std::out_of_range("Index out of range");
        if (index != mSize - 1) {
            mData[index] = std::move(mData[mSize - 1]);
        }
        PopBack();
    }

    bool RemoveSwap(const T& value) {
        const SizeType index = IndexOf(value);
        if (index == InvalidIndex) return false;
        RemoveAtSwap(index);
        return true;
    }

    // Utility methods - Queries
    static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

    [[nodiscard]] bool IsValidIndex(SizeType index) const { return index < mSize; }

    template<typename Predicate>
    SizeType IndexOfIf(Predicate pred) const {
        for (SizeType i = 0; i < mSize; ++i) {
            if (pred(mData[i])) return i;
        }
        return InvalidIndex;
    }

    template<typename Predicate>
    bool ContainsIf(Predicate pred) const { return IndexOfIf(pred) != InvalidIndex; }

    template<typename Predicate>
    bool Any(Predicate pred) const { return IndexOfIf(pred) != InvalidIndex; }

    template<typename Predicate>
    bool All(Predicate pred) const {
        for (SizeType i = 0; i < mSize; ++i) {
            if (!pred(mData[i])) return false;
        }
        return true;
    }

    template<typename Predicate>
    SizeType CountIf(Predicate pred) const {
        SizeType count = 0;
        for (SizeType i = 0; i < mSize; ++i) {
            if (pred(mData[i])) ++count;
        }
        return count;
    }

    SizeType Count(const T& value) const {
        SizeType count = 0;
        for (SizeType i = 0; i < mSize; ++i) {
            if (mData[i] == value) ++count;
        }
        return count;
    }

    // Min/Max by a "less than" comparator; End() when empty.
    template<typename Comparator>
    Iterator MinElement(Comparator comp) {
        if (mSize == 0) return End();
        SizeType best = 0;
        for (SizeType i = 1; i < mSize; ++i) {
            if (comp(mData[i], mData[best])) best = i;
        }
        return &mData[best];
    }

    template<typename Comparator>
    Iterator MaxElement(Comparator comp) {
        if (mSize == 0) return End();
        SizeType best = 0;
        for (SizeType i = 1; i < mSize; ++i) {
            if (comp(mData[best], mData[i])) best = i;
        }
        return &mData[best];
    }

    Iterator MinElement() { return MinElement([](const T& a, const T& b) { return a < b; }); }
    Iterator MaxElement() { return MaxElement([](const T& a, const T& b) { return a < b; }); }

    // Sum of elements; R avoids overflow (e.g. Sum<UInt64>()).
    template<typename R = T>
    R Sum() const {
        R total{};
        for (SizeType i = 0; i < mSize; ++i) total += static_cast<R>(mData[i]);
        return total;
    }

    // Utility methods - Mutation
    // Pushes only when the value is not present yet; true = added.
    bool AddUnique(const T& value) {
        if (Contains(value)) return false;
        PushBack(value);
        return true;
    }

    void SwapItems(SizeType a, SizeType b) {
        if (a == b || a >= mSize || b >= mSize) return;
        T temp = std::move(mData[a]);
        mData[a] = std::move(mData[b]);
        mData[b] = std::move(temp);
    }

    void Swap(DynamicArray& other) noexcept {
        std::swap(mData, other.mData);
        std::swap(mSize, other.mSize);
        std::swap(mCapacity, other.mCapacity);
        std::swap(mAllocator, other.mAllocator);
    }

    // Overwrites all existing elements (does not change the size).
    void Fill(const T& value) {
        for (SizeType i = 0; i < mSize; ++i) mData[i] = value;
    }

    // Utility methods - Transformations (return new arrays)
    template<typename Predicate>
    DynamicArray Filter(Predicate pred) const {
        DynamicArray result(mAllocator);
        for (SizeType i = 0; i < mSize; ++i) {
            if (pred(mData[i])) result.PushBack(mData[i]);
        }
        return result;
    }

    // 1:1 mapping; the result type is deduced from the function.
    template<typename Func>
    auto Map(Func func) const -> DynamicArray<decltype(func(std::declval<const T&>()))> {
        DynamicArray<decltype(func(std::declval<const T&>()))> result;
        result.Reserve(mSize);
        for (SizeType i = 0; i < mSize; ++i) result.PushBack(func(mData[i]));
        return result;
    }

    // Folds into a single value: acc = func(acc, item), front to back.
    // The accumulator type comes from `init`, so e.g. Reduce(String(), ...) joins strings.
    template<typename R, typename Func>
    R Reduce(R init, Func func) const {
        R acc = std::move(init);
        for (SizeType i = 0; i < mSize; ++i) acc = func(std::move(acc), mData[i]);
        return acc;
    }

    // Copy of the first / last n elements; n larger than the size = the whole array.
    DynamicArray First(SizeType count) const {
        return Slice(0, count);
    }

    DynamicArray Last(SizeType count) const {
        if (count >= mSize) return Slice(0);
        return Slice(mSize - count);
    }

    // Copy of a sub-range; running past the end is clamped, not thrown on.
    DynamicArray Slice(SizeType start, SizeType count = InvalidIndex) const {
        DynamicArray result(mAllocator);
        if (start >= mSize) return result;

        const SizeType available = mSize - start;
        const SizeType take = count < available ? count : available;
        result.Reserve(take);
        for (SizeType i = 0; i < take; ++i) result.PushBack(mData[start + i]);
        return result;
    }

    // Comparisons
    bool operator==(const DynamicArray& other) const {
        if (mSize != other.mSize) return false;
        for (SizeType i = 0; i < mSize; ++i) {
            if (!(mData[i] == other.mData[i])) return false;
        }
        return true;
    }

    bool operator!=(const DynamicArray& other) const { return !(*this == other); }

private:
    // Makes room for one more element. false = the allocator refused, and the caller
    // must drop the operation rather than write past the end. This mirrors what the
    // rest of PluSTL does on allocation failure (HashSet::Rehash keeps the old table
    // and lets the insert fail) — a failed Reserve is a no-op, so without this check
    // a push would happily construct one element past the capacity.
    bool GrowForPush() {
        if (mSize < mCapacity) return true;
        Reserve(mCapacity == 0 ? 2 : mCapacity * 2);
        return mSize < mCapacity;
    }

    T* mData;
    SizeType mSize;
    SizeType mCapacity;
    [[no_unique_address]] Allocator mAllocator;

    template<typename Comparator>
    void QuickSort(SizeType low, SizeType high, Comparator comp) {
        if (low < high) {
            SizeType pi = Partition(low, high, comp);
            if (pi > 0) QuickSort(low, pi - 1, comp);
            QuickSort(pi + 1, high, comp);
        }
    }

    template<typename Comparator>
    SizeType Partition(SizeType low, SizeType high, Comparator comp) {
        T& pivot = mData[high];
        SizeType i = low;

        for (SizeType j = low; j < high; ++j) {
            if (comp(mData[j], pivot)) {
                T temp = std::move(mData[i]);
                mData[i] = std::move(mData[j]);
                mData[j] = std::move(temp);
                ++i;
            }
        }

        T temp = std::move(mData[i]);
        mData[i] = std::move(mData[high]);
        mData[high] = std::move(temp);

        return i;
    }
};

} // namespace Plu

// Transitional: DynamicArray used to live in the global namespace and is named
// unqualified in well over a hundred files. The using-declaration keeps that
// spelling working while call sites move to Plu::DynamicArray.
using Plu::DynamicArray;
