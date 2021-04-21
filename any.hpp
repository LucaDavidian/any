#ifndef ANY_H
#define ANY_H

#include <cstddef>
#include <type_traits>
#include <utility>
#include <exception>

class BadCastException : public std::exception
{
public:
    const char *what() const noexcept override
    {
        return "wrong type from Get";
    }
};

using std::size_t;

template <size_t SIZE, size_t ALIGNMENT = alignof(std::max_align_t)>
struct AlignedStorage
{
    //alignas(ALIGNMENT)
    struct Type
    {
        alignas(ALIGNMENT) unsigned char storage[SIZE]; 
    };
};

template <size_t SIZE, size_t ALIGNMENT = alignof(std::max_align_t)>
using AlignedStorageT = typename AlignedStorage<SIZE, ALIGNMENT>::Type;

template <size_t>
class Any;

template <size_t SIZE>
void swap(Any<SIZE> &a, Any<SIZE> &b)
{
    a.Swap(b);
}

/* 
 * A handle is used to construct/assign a non-managing Any 
 * (Any contains a reference and doesn't manage the lifetime of the object).
 */
template <typename T>
class Handle
{
template <size_t> friend class Any;

public:
    Handle(T &object) : mReference(&object) {}

    template <size_t SIZE>
    Handle(const Any<SIZE> &object) : mReference(&object.template Get<T>()) {}
private:
    T *mReference;
};

typedef Any<8> any;

template <size_t SIZE>
class Any
{
template <typename> friend class Handle;

private:
    class VTable
    {
    public:
        virtual void *Copy(const void *from) = 0;             // allocation copy
        virtual void Copy(void *to, const void *from) = 0;    // SBO copy

        virtual void *Move(const void *from) = 0;             // allocation move
        virtual void Move(void *to, const void *from) = 0;    // SBO move

        virtual void Destroy(void *object, bool SBO) = 0;     // allocation and SBO 
    protected:
        VTable() = default;
    };

    template <typename T>
    class VTableT : public VTable
    {
    public:
        static VTableT<T> mVTable;

        void *Copy(const void *from) override;
        void Copy(void *to, const void *from) override;

        void *Move(const void *from) override;
        void Move(void *to, const void *from) override;

        void Destroy(void *object, bool SBO) override;
    private:
        VTableT() = default;
    };

    template <typename T>
    class VTableT<Handle<T>> : public VTable
    {
    public:
        static VTableT mVTable;

        void *Copy(const void *from) override { return const_cast<void*>(from); }
        void Copy(void *to, const void *from) override { /* not used */ }

        void *Move(const void *from) override { return const_cast<void*>(from); }
        void Move(void *to, const void *from) override { /* not used */ }

        void Destroy(void *object, bool SBO) override { /* do nothing */ }
    private:
        VTableT() = default;
    };

public:
    Any() : mVTable(nullptr), mObject(nullptr), mSBO(false) {}

    Any(const Any &other);
    
    Any(Any &&other);

    // SFINAE'd out if allocating
    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Any<SIZE>>::value && (sizeof(typename std::decay<T>::type) <= SIZE)>::type>
    Any(T &&object);

    // SFINAE'd out if using small buffer optimization
    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Any<SIZE>>::value && (sizeof(typename std::decay<T>::type) > SIZE)>::type, typename = void>
    Any(T &&object);

    template <typename T>
    Any(Handle<T> handle) : Any()
    {
        mObject = handle.mReference;
        mVTable = &VTableT<Handle<T>>::mVTable;
    }

    ~Any();

    Any &operator=(const Any &other);

    Any &operator=(Any &&other);

    template <typename T, typename = typename std::enable_if<!std::is_same<typename std::decay<T>::type, Any>::value>::type>
    Any &operator=(T &&object);

    template <typename T>
    Any &operator=(Handle<T> handle)
    {
        Any temp(handle);

        Swap(temp);

        return *this;
    }

    explicit operator bool() const { return mVTable; }

    void Swap(Any &other);

    template <typename T>
    bool Is() const
    {
        return mVTable == &VTableT<Handle<T>>::mVTable || mVTable == &VTableT<T>::mVTable;
    }

    template <typename T>
    const T &Get() const 
    {
        if (mSBO)
            return *reinterpret_cast<const T*>(&mStorage);
        else
            return *static_cast<T*>(mObject);
    }

    template <typename T>
    T &Get()
    {
        //return const_cast<T&>(std::as_const(*this).Get<T>());
        return const_cast<T&>(static_cast<const Any&>(*this).Get<T>());
    }

    template <typename T>
    const T *TryGet() const 
    {
        if (!Is<T>())
            //throw BadCastException();
            return nullptr;

        if (mSBO)
            return reinterpret_cast<const T*>(&mStorage);
        else
            return static_cast<T*>(mObject);
    }

    template <typename T>
    T *TryGet()
    {
        //return const_cast<T&>(std::as_const(*this).Get<T>());
        return const_cast<T*>(static_cast<const Any&>(*this).TryGet<T>());
    }

private:
    VTable *mVTable;

    union
    {
        void *mObject;
        AlignedStorageT<SIZE> mStorage;
    };

    bool mSBO;
};

/**** VTable implementation ****/
template <size_t SIZE>
template <typename T>
void *Any<SIZE>::VTableT<T>::Copy(const void *from)
{
    return new T(*static_cast<T const*>(from));
}

template <size_t SIZE>
template <typename T>
void Any<SIZE>::VTableT<T>::Copy(void *to, const void *from)
{
    new(to) T(*static_cast<T const*>(from));
}

template <size_t SIZE>
template <typename T>
void *Any<SIZE>::VTableT<T>::Move(const void *from)
{
    return new T(std::move(*static_cast<T const*>(from)));
}

template <size_t SIZE>
template <typename T>
void Any<SIZE>::VTableT<T>::Move(void *to, const void *from)
{
    new(to) T(std::move(*static_cast<T const*>(from)));
}

template <size_t SIZE>
template <typename T>
void Any<SIZE>::VTableT<T>::Destroy(void *object, bool SBO)
{
    if (SBO)
        static_cast<T*>(object)->~T();
    else
        delete static_cast<T*>(object);
}

/**** Any implementation ****/
template <size_t SIZE>
Any<SIZE>::Any(Any const &other) : Any()
{
    if (!other.mVTable)  // empty Any
        return;

    if (other.mSBO)
        other.mVTable->Copy(&mStorage, &other.mStorage);
    else
        mObject = other.mVTable->Copy(other.mObject);

    mSBO = other.mSBO;
    mVTable = other.mVTable;
}

template <size_t SIZE>
Any<SIZE>::Any(Any &&other) : Any()
{
    if (!other.mVTable)  // empty Any
        return;

    if (other.mSBO)
        other.mVTable->Move(&mStorage, &other.mStorage);
    else
        mObject = other.mVTable->Move(other.mObject);

    mSBO = other.mSBO;
    mVTable = other.mVTable;
}


template <size_t SIZE>
template <typename T, typename>
Any<SIZE>::Any(T &&object) : Any()
{
    using T_ = std::decay_t<T>;  // T can be deduced as T& (lvalue) or T (rvalue)

    new(&mStorage) T_(std::forward<T>(object));
    mSBO = true;
    
    mVTable = &VTableT<T_>::mVTable;
}

template <size_t SIZE>
template <typename T, typename, typename>
Any<SIZE>::Any(T &&object) : Any()
{
    using T_ = std::decay_t<T>;  // T can be deduced as T& (lvalue) or T (rvalue)

    mObject = new T_(std::forward<T>(object));
    mSBO = false;

    mVTable = &VTableT<T_>::mVTable;
}

template <size_t SIZE>
Any<SIZE>::~Any()
{
    if (mVTable)
    {
        if (mSBO)
            mVTable->Destroy(&mStorage, true);
        else
            mVTable->Destroy(mObject, false);
    }
}

template <size_t SIZE>
Any<SIZE> &Any<SIZE>::operator=(const Any &other)
{
    Any temp(other);

    Swap(temp);

    return *this;
}

template <size_t SIZE>
Any<SIZE> &Any<SIZE>::operator=(Any &&other)
{
    Any temp(std::move(other));

    Swap(temp);

    return *this;
}

template <size_t SIZE>
template <typename T, typename>
Any<SIZE> &Any<SIZE>::operator=(T &&object) 
{
    using T_ = typename std::decay<T>::type;

    if (mVTable)  // Any is not empty
    {
        if (Is<T_>())  // if (mVTable == &VTableT<T_>::mVTable) if Any contains same type assign 
        {
            if (mSBO)
                *reinterpret_cast<T_*>(&mStorage) = std::forward<T>(object);
            else
                *static_cast<T_*>(mObject) = std::forward<T>(object);
        }
        else  // if Any does not contain same type destroy previous object and copy new object
        {
            if (mSBO)
                mVTable->Destroy(&mStorage, true);
            else
                mVTable->Destroy(mObject, false);

            if constexpr (sizeof(T_) <= SIZE) // constexpr if to remove warning from compiler
            {
                new(&mStorage) T_(std::forward<T>(object));
                mSBO = true;
            }
            else
            {   
                mObject = new T_(std::forward<T>(object));
                mSBO = false;
            }

            mVTable = &VTableT<T_>::mVTable;
        }
    }
    else  // if this is an empty Any copy object
    {
        if constexpr (sizeof(T_) < SIZE) // constexpr if to remove warning from compiler
        {
            new(&mStorage) T_(std::forward<T>(object));
            mSBO = true;
        }
        else
        {   
            mObject = new T_(std::forward<T>(object));
            mSBO = false;
        }

        mVTable = &VTableT<T_>::mVTable;
    }

    return *this;
}

template <size_t SIZE>
void Any<SIZE>::Swap(Any &other)
{
    // swap object and SBO tag
    if (mSBO)
        if (other.mSBO)
        {
            AlignedStorageT<SIZE> storageTemp;

            mVTable->Move(&storageTemp, &mStorage);
            other.mVTable->Move(&mStorage, &other.mStorage);
            mVTable->Move(&other.mStorage, &storageTemp);
        }
        else
        {
            mObject = other.mObject;
            mSBO = false;

            mVTable->Move(&other.mStorage, &mStorage);
            other.mSBO = true;
        }
    else
        if (other.mSBO)
        {
            other.mVTable->Move(&mStorage, &other.mStorage);
            mSBO = true;

            other.mObject = mObject;
            other.mSBO = false;
        }
        else
        {
            void *objectTemp = mObject;
            mObject = other.mObject;
            other.mObject = objectTemp;
        }

    // swap vtable
    VTable *vTableTemp = mVTable;
    mVTable = other.mVTable;
    other.mVTable = vTableTemp;
}

template <size_t SIZE>
template <typename T>
Any<SIZE>::VTableT<T> Any<SIZE>::VTableT<T>::mVTable; 

template <size_t SIZE>
template <typename T>
Any<SIZE>::VTableT<Handle<T>> Any<SIZE>::VTableT<Handle<T>>::mVTable;

#endif  // ANY_H