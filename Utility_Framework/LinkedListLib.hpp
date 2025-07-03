#ifndef _LINKEDLIST_HPP
#define _LINKEDLIST_HPP
template <typename _Type>
class LinkedList;

template <typename _Type>
struct LinkProperty
{
	LinkProperty* _pPrev{ nullptr };
	LinkProperty* _pNext{ nullptr };
	_Type* _pElement{ nullptr };

public:
	LinkProperty() = delete;
	LinkProperty(_Type* pType) : _pElement(pType) {};
	virtual ~LinkProperty() = default;

private:
	friend class LinkedList<_Type>;

private:
	void InternalSeparate()
	{
		_pPrev = nullptr;
		_pNext = nullptr;
	}
};

template <typename _Type>
class LinkedList
{
public:
	class Iterator
	{
	public:
		Iterator(LinkProperty<_Type>* pProperty) : _pProperty(pProperty) {}
		Iterator& operator++()
		{
			_pProperty = _pProperty->_pNext;
			return *this;
		}

		_Type& operator*()
		{
			return (*_pProperty->_pElement);
		}

		bool operator!=(const Iterator& other)
		{
			return _pProperty != other._pProperty;
		}

		LinkProperty<_Type>* operator->()
		{
			return _pProperty;
		}

	private:
		LinkProperty<_Type>* _pProperty{};
	};

	const Iterator begin()
	{
		return Iterator(_pHead);
	}

	const Iterator end()
	{
		return Iterator(nullptr);
	}

	void Link(LinkProperty<_Type>* pNewLinkProperty)
	{
		if (!_pHead)
		{
			_pHead = pNewLinkProperty;
			_pTail = pNewLinkProperty;
		}
		else
		{
			pNewLinkProperty->_pPrev = _pTail;
			_pTail->_pNext = pNewLinkProperty;
			_pTail = pNewLinkProperty;
		}
		_size++;
	}

	void Unlink(LinkProperty<_Type>* pDeleteLinkProperty)
	{
		LinkProperty<_Type>*& LinkageFront = pDeleteLinkProperty->_pPrev;
		LinkProperty<_Type>*& LinkageBack = pDeleteLinkProperty->_pNext;

		if (LinkageFront)
		{
			LinkageFront->_pNext = LinkageBack;
		}
		else
		{
			_pHead = LinkageBack;
		}

		if (LinkageBack)
		{
			LinkageBack->_pPrev = LinkageFront;
		}
		else
		{
			_pTail = LinkageFront;
		}

		pDeleteLinkProperty->InternalSeparate();
		_size--;
	}

	void erase(Iterator& begin, Iterator& end)
	{
		LinkProperty<_Type>* LinkageCurrent = begin->_pProperty;
		LinkProperty<_Type>* LinkageNext = nullptr;
		while (LinkageCurrent && LinkageCurrent != end->_pProperty)
		{
			LinkageNext = LinkageCurrent->_pNext;
			LinkageCurrent->InternalSeparate();
			LinkageCurrent = LinkageNext;
		}
	}

	void ClearLink()
	{
		LinkProperty<_Type>* LinkageCurrent = _pHead;
		LinkProperty<_Type>* LinkageNext = nullptr;
		while (LinkageCurrent)
		{
			LinkageNext = LinkageCurrent->_pNext;
			LinkageCurrent->InternalSeparate();
			LinkageCurrent = LinkageNext;
		}
		_pHead = nullptr;
		_pTail = nullptr;
		_size = 0;
	}

	size_t size() const { return _size; }


private:
	LinkProperty<_Type>* _pHead{};
	LinkProperty<_Type>* _pTail{};
	size_t _size{ 0 };
};

#endif // !_LINKEDLIST_HPP