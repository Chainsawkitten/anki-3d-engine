// Copyright (C) 2009-2017, Panagiotis Christopoulos Charitos and contributors.
// All rights reserved.
// Code licensed under the BSD License.
// http://www.anki3d.org/LICENSE

#pragma once

#include <anki/util/Allocator.h>
#include <anki/util/Functions.h>

namespace anki
{

/// @addtogroup util_containers
/// @{

/// The base of DynamicArray and WeakArray.
template<typename T>
class DynamicArrayBase
{
public:
	using Value = T;
	using Iterator = Value*;
	using ConstIterator = const Value*;
	using Reference = Value&;
	using ConstReference = const Value&;

	Reference operator[](const PtrSize n)
	{
		ANKI_ASSERT(n < m_size);
		return m_data[n];
	}

	ConstReference operator[](const PtrSize n) const
	{
		ANKI_ASSERT(n < m_size);
		return m_data[n];
	}

	Iterator getBegin()
	{
		return m_data;
	}

	ConstIterator getBegin() const
	{
		return m_data;
	}

	Iterator getEnd()
	{
		return m_data + m_size;
	}

	ConstIterator getEnd() const
	{
		return m_data + m_size;
	}

	/// Make it compatible with the C++11 range based for loop.
	Iterator begin()
	{
		return getBegin();
	}

	/// Make it compatible with the C++11 range based for loop.
	ConstIterator begin() const
	{
		return getBegin();
	}

	/// Make it compatible with the C++11 range based for loop.
	Iterator end()
	{
		return getEnd();
	}

	/// Make it compatible with the C++11 range based for loop.
	ConstIterator end() const
	{
		return getEnd();
	}

	/// Get first element.
	Reference getFront()
	{
		ANKI_ASSERT(!isEmpty());
		return m_data[0];
	}

	/// Get first element.
	ConstReference getFront() const
	{
		ANKI_ASSERT(!isEmpty());
		return m_data[0];
	}

	/// Get last element.
	Reference getBack()
	{
		ANKI_ASSERT(!isEmpty());
		return m_data[m_size - 1];
	}

	/// Get last element.
	ConstReference getBack() const
	{
		ANKI_ASSERT(!isEmpty());
		return m_data[m_size - 1];
	}

	PtrSize getSize() const
	{
		return m_size;
	}

	PtrSize getByteSize() const
	{
		return m_size * sizeof(Value);
	}

	Bool isEmpty() const
	{
		return m_size == 0;
	}

	PtrSize getSizeInBytes() const
	{
		return m_size * sizeof(Value);
	}

protected:
	DynamicArrayBase(Value* v, PtrSize size)
		: m_data(v)
		, m_size(size)
	{
	}

	Value* m_data;
	PtrSize m_size;
};

/// Dynamic array with manual destruction. It doesn't hold the allocator and that makes it compact. At the same time
/// that requires manual destruction. Used in permanent classes.
template<typename T>
class DynamicArray : public DynamicArrayBase<T>
{
public:
	using Base = DynamicArrayBase<T>;
	using Base::m_data;
	using Base::m_size;
	using typename Base::Value;

	static constexpr F32 GROW_SCALE = 2.0f;
	static constexpr F32 SHRINK_SCALE = 2.0f;

	DynamicArray()
		: Base(nullptr, 0)
	{
	}

	// Non-copyable
	DynamicArray(const DynamicArray& b) = delete;

	/// Move.
	DynamicArray(DynamicArray&& b)
		: DynamicArray()
	{
		*this = std::move(b);
	}

	~DynamicArray()
	{
		ANKI_ASSERT(m_data == nullptr && m_size == 0 && m_capacity == 0 && "Requires manual destruction");
	}

	/// Move.
	DynamicArray& operator=(DynamicArray&& b)
	{
		ANKI_ASSERT(m_data == nullptr && m_size == 0 && "Cannot move before destroying");
		m_data = b.m_data;
		b.m_data = nullptr;
		m_size = b.m_size;
		b.m_size = 0;
		m_capacity = b.m_capacity;
		b.m_capacity = 0;
		return *this;
	}

	// Non-copyable
	DynamicArray& operator=(const DynamicArray& b) = delete;

	/// Only create the array. Useful if @a T is non-copyable or movable .
	template<typename TAllocator>
	void create(TAllocator alloc, PtrSize size)
	{
		ANKI_ASSERT(m_data == nullptr && m_size == 0 && m_capacity == 0);
		if(size > 0)
		{
			m_data = alloc.template newArray<Value>(size);
			m_size = size;
			m_capacity = size;
		}
	}

	/// Only create the array. Useful if @a T is non-copyable or movable .
	template<typename TAllocator>
	void create(TAllocator alloc, PtrSize size, const Value& v)
	{
		ANKI_ASSERT(m_data == nullptr && m_size == 0 && m_capacity == 0);
		if(size > 0)
		{
			m_data = alloc.template newArray<Value>(size, v);
			m_size = size;
			m_capacity = size;
		}
	}

	/// Grow or create the array. @a T needs to be copyable and moveable.
	template<typename TAllocator>
	void resize(TAllocator alloc, PtrSize size, const Value& v);

	/// Grow or create the array. @a T needs to be copyable and moveable.
	template<typename TAllocator>
	void resize(TAllocator alloc, PtrSize size);

	/// Push back value.
	template<typename TAllocator, typename... TArgs>
	Value& emplaceBack(TAllocator alloc, TArgs&&... args)
	{
		resizeStorage(alloc, m_size + 1);
		::new(&m_data[m_size]) Value(std::forward<TArgs>(args)...);
		++m_size;
		return m_data[m_size - 1];
	}

	/// Destroy the array.
	template<typename TAllocator>
	void destroy(TAllocator alloc)
	{
		if(m_data)
		{
			ANKI_ASSERT(m_size > 0);
			ANKI_ASSERT(m_capacity > 0);
			alloc.deleteArray(m_data, m_size);

			m_data = nullptr;
			m_size = 0;
			m_capacity = 0;
		}

		ANKI_ASSERT(m_data == nullptr && m_size == 0 && m_capacity == 0);
	}

	/// Validate it. Will only work when assertions are enabled.
	void validate() const
	{
		if(m_data)
		{
			ANKI_ASSERT(m_size > 0 && m_capacity > 0);
			ANKI_ASSERT(m_size <= m_capacity);
		}
		else
		{
			ANKI_ASSERT(m_size == 0 && m_capacity == 0);
		}
	}

	/// Move the data from this object. It's like moving (operator or constructor) but instead of moving to another
	/// object of the same type it moves to 3 values.
	void moveAndReset(Value*& data, PtrSize& size, PtrSize& storageSize)
	{
		data = m_data;
		size = m_size;
		storageSize = m_capacity;
		m_data = nullptr;
		m_size = 0;
		m_capacity = 0;
	}

protected:
	PtrSize m_capacity = 0;

private:
	/// Resizes the storage but doesn't constructs any elements. Only moves them.
	template<typename TAllocator>
	void resizeStorage(TAllocator& alloc, PtrSize size);
};

/// Dynamic array with automatic destruction. It's the same as DynamicArray but it holds the allocator in order to
/// perform automatic destruction. Use it for temp operations and on transient classes.
template<typename T>
class DynamicArrayAuto : public DynamicArray<T>
{
public:
	using Base = DynamicArray<T>;
	using Base::m_capacity;
	using Base::m_data;
	using Base::m_size;
	using typename Base::Value;

	template<typename TAllocator>
	DynamicArrayAuto(TAllocator alloc)
		: Base()
		, m_alloc(alloc)
	{
	}

	/// Move.
	DynamicArrayAuto(DynamicArrayAuto&& b)
		: Base()
	{
		*this = std::move(b);
	}

	~DynamicArrayAuto()
	{
		Base::destroy(m_alloc);
	}

	/// Move.
	DynamicArrayAuto& operator=(DynamicArrayAuto&& b)
	{
		Base::destroy(m_alloc);

		m_data = b.m_data;
		b.m_data = nullptr;
		m_size = b.m_size;
		b.m_size = 0;
		m_capacity = b.m_capacity;
		b.m_capacity = 0;
		m_alloc = b.m_alloc;
		b.m_alloc = {};
		return *this;
	}

	/// @copydoc DynamicArray::create
	void create(PtrSize size)
	{
		Base::create(m_alloc, size);
	}

	/// @copydoc DynamicArray::create
	void create(PtrSize size, const Value& v)
	{
		Base::create(m_alloc, size, v);
	}

	/// @copydoc DynamicArray::resize
	void resize(PtrSize size, const Value& v)
	{
		Base::resize(m_alloc, size, v);
	}

	/// @copydoc DynamicArray::emplaceBack
	template<typename... TArgs>
	void emplaceBack(TArgs&&... args)
	{
		Base::emplaceBack(m_alloc, std::forward<TArgs>(args)...);
	}

	/// @copydoc DynamicArray::resize
	void resize(PtrSize size)
	{
		Base::resize(m_alloc, size);
	}

	/// @copydoc DynamicArray::moveAndReset
	void moveAndReset(Value*& data, PtrSize& size, PtrSize& storageSize)
	{
		Base::moveAndReset(data, size, storageSize);
		// Don't touch the m_alloc
	}

private:
	GenericMemoryPoolAllocator<T> m_alloc;
};

/// Array with preallocated memory.
template<typename T>
class WeakArray : public DynamicArrayBase<T>
{
public:
	using Base = DynamicArrayBase<T>;
	using Base::m_data;
	using Base::m_size;

	WeakArray()
		: Base(nullptr, 0)
	{
	}

	WeakArray(T* mem, PtrSize size)
		: Base(mem, size)
	{
		if(size)
		{
			ANKI_ASSERT(mem);
		}
	}

	template<typename Y, PtrSize S>
	WeakArray(const Array<Y, S>& arr)
		: Base(&arr[0], S)
	{
	}

	template<typename Y>
	WeakArray(const DynamicArrayBase<Y>& arr)
		: Base((arr.getSize()) ? &arr[0] : nullptr, arr.getSize())
	{
	}

	/// Copy.
	WeakArray(const WeakArray& b)
		: Base(b.m_data, b.m_size)
	{
	}

	/// Move.
	WeakArray(WeakArray&& b)
		: WeakArray()
	{
		*this = std::move(b);
	}

	/// Copy.
	WeakArray& operator=(const WeakArray& b)
	{
		m_data = b.m_data;
		m_size = b.m_size;
		return *this;
	}

	/// Move.
	WeakArray& operator=(WeakArray&& b)
	{
		m_data = b.m_data;
		b.m_data = nullptr;
		m_size = b.m_size;
		b.m_size = 0;
		return *this;
	}
};
/// @}

} // end namespace anki

#include <anki/util/DynamicArray.inl.h>
