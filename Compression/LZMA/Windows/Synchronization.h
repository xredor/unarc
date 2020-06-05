// Windows/Synchronization.h

#ifndef __WINDOWS_SYNCHRONIZATION_H
#define __WINDOWS_SYNCHRONIZATION_H

#ifdef FREEARC_WIN

#include "Defs.h"
#include "Handle.h"

namespace NWindows {
namespace NSynchronization {

class CObject: public CHandle
{
public:
  bool Lock(DWORD timeoutInterval = INFINITE)
    { return (::WaitForSingleObject(_handle, timeoutInterval) == WAIT_OBJECT_0); }
};

class CBaseEvent: public CObject
{
public:
  bool Create(bool manualReset, bool initiallyOwn, LPCTSTR name = NULL,
      LPSECURITY_ATTRIBUTES securityAttributes = NULL)
  {
    _handle = ::CreateEvent(securityAttributes, BoolToBOOL(manualReset),
        BoolToBOOL(initiallyOwn), name);
    return (_handle != 0);
  }

  bool Open(DWORD desiredAccess, bool inheritHandle, LPCTSTR name)
  {
    _handle = ::OpenEvent(desiredAccess, BoolToBOOL(inheritHandle), name);
    return (_handle != 0);
  }

  bool Set() { return BOOLToBool(::SetEvent(_handle)); }
  bool Pulse() { return BOOLToBool(::PulseEvent(_handle)); }
  bool Reset() { return BOOLToBool(::ResetEvent(_handle)); }
};

class CEvent: public CBaseEvent
{
public:
  CEvent() {};
  CEvent(bool manualReset, bool initiallyOwn,
      LPCTSTR name = NULL, LPSECURITY_ATTRIBUTES securityAttributes = NULL)
  {
    if (!Create(manualReset, initiallyOwn, name, securityAttributes))
      CHECK(FALSE,(s,"CreateEvent error"));
  };
};

class CManualResetEvent: public CEvent
{
public:
  CManualResetEvent(bool initiallyOwn = false, LPCTSTR name = NULL,
      LPSECURITY_ATTRIBUTES securityAttributes = NULL):
    CEvent(true, initiallyOwn, name, securityAttributes) {};
};

class CAutoResetEvent: public CEvent
{
public:
  CAutoResetEvent(bool initiallyOwn = false, LPCTSTR name = NULL,
      LPSECURITY_ATTRIBUTES securityAttributes = NULL):
    CEvent(false, initiallyOwn, name, securityAttributes) {};
};

class CMutex: public CObject
{
public:
  bool Create(bool initiallyOwn, LPCTSTR name = NULL,
      LPSECURITY_ATTRIBUTES securityAttributes = NULL)
  {
    _handle = ::CreateMutex(securityAttributes, BoolToBOOL(initiallyOwn), name);
    return (_handle != 0);
  }
  bool Open(DWORD desiredAccess, bool inheritHandle, LPCTSTR name)
  {
    _handle = ::OpenMutex(desiredAccess, BoolToBOOL(inheritHandle), name);
    return (_handle != 0);
  }
  bool Release() { return BOOLToBool(::ReleaseMutex(_handle)); }
};

class CMutexLock
{
  CMutex *_object;
public:
  CMutexLock(CMutex &object): _object(&object) { _object->Lock(); }
  ~CMutexLock() { _object->Release(); }
};

class CSemaphore: public CObject
{
public:
  bool Create(LONG initiallyCount, LONG maxCount, LPCTSTR name = NULL,
      LPSECURITY_ATTRIBUTES securityAttributes = NULL)
  {
    _handle = ::CreateSemaphore(securityAttributes, initiallyCount, maxCount, name);
    return (_handle != 0);
  }
  bool Open(DWORD desiredAccess, bool inheritHandle, LPCTSTR name)
  {
    _handle = ::OpenSemaphore(desiredAccess, BoolToBOOL(inheritHandle), name);
    return (_handle != 0);
  }
  bool Release(LONG releaseCount = 1, LPLONG previousCount = NULL)
  {
    return BOOLToBool(::ReleaseSemaphore(_handle, releaseCount, previousCount));
  }
};

class CCriticalSection
{
  CRITICAL_SECTION _object;
  // void Initialize() { ::InitializeCriticalSection(&_object); }
  // void Delete() { ::DeleteCriticalSection(&_object); }
public:
  CCriticalSection() { ::InitializeCriticalSection(&_object); }
  ~CCriticalSection() { ::DeleteCriticalSection(&_object); }
  void Enter() { ::EnterCriticalSection(&_object); }
  void Leave() { ::LeaveCriticalSection(&_object); }
};

class CCriticalSectionLock
{
  CCriticalSection *_object;
  void Unlock()  { _object->Leave(); }
public:
  CCriticalSectionLock(CCriticalSection &object): _object(&object) {_object->Enter(); }
  ~CCriticalSectionLock() { Unlock(); }
};

}}


#else  // !FREEARC_WIN

#include "Defs.h"

#ifdef ENV_BEOS
#include <Locker.h>
#include <kernel/OS.h>
#include <list>
#endif

#undef DEBUG_SYNCHRO

DWORD WINAPI WaitForMultipleObjects( DWORD count, const HANDLE *handles, BOOL wait_all, DWORD timeout );

namespace NWindows {
namespace NSynchronization {

struct CBaseHandle
{
	typedef enum { EVENT , SEMAPHORE } t_type;

	CBaseHandle(t_type t) { type = t;  }

	t_type type;
	union
	{
		struct
		{
			bool _manual_reset;
			bool _state;
		} event;
		struct
		{
			LONG count;
			LONG maxCount;
		} sema;
	} u;
  operator HANDLE() { return ((HANDLE)this); }
  bool Close() { return true; }
};

class CBaseEvent : public CBaseHandle
{
public:

  CBaseEvent() : CBaseHandle(CBaseHandle::EVENT) {}
  ~CBaseEvent() { Close(); }

  bool Create(bool manualReset, bool initiallyOwn)
  {
    this->u.event._manual_reset = manualReset;
    this->u.event._state        = initiallyOwn;
    return true;
  }

  bool Set();
  bool Reset();

  bool Lock();

};

class CEvent: public CBaseEvent
{
public:
  CEvent() {};
  CEvent(bool manualReset, bool initiallyOwn);
};

class CManualResetEvent: public CEvent
{
public:
  CManualResetEvent(bool initiallyOwn = false):
    CEvent(true, initiallyOwn) {};
};

class CAutoResetEvent: public CEvent
{
public:
  CAutoResetEvent(bool initiallyOwn = false):
    CEvent(false, initiallyOwn) {};
};

#ifdef ENV_BEOS
class CCriticalSection : BLocker
{
  std::list<thread_id> _waiting;
public:
  CCriticalSection() {}
  ~CCriticalSection() {}
  void Enter() { Lock(); }
  void Leave() { Unlock(); }
  void WaitCond() {
    _waiting.push_back(find_thread(NULL));
    thread_id sender;
    Unlock();
    int msg = receive_data(&sender, NULL, 0);
    Lock();
  }
  void SignalCond() {
    Lock();
    for (std::list<thread_id>::iterator index = _waiting.begin(); index != _waiting.end(); index++) {
      send_data(*index, '7zCN', NULL, 0);
    }
   _waiting.clear();
    Unlock();
  }
};
#else
#ifdef DEBUG_SYNCHRO
class CCriticalSection
{
  pthread_mutex_t _object;
  pthread_cond_t _cond;
  void dump_error(int ret,const char *text)
  {
    printf("\n##ERROR %s : ret = %d (%s)##\n",text,ret,strerror(ret));
    // abort();
  }
public:
  CCriticalSection() {
    pthread_mutexattr_t mutexattr;
    int ret = pthread_mutexattr_init(&mutexattr);
    if (ret != 0) dump_error(ret,"pthread_mutexattr_init");
    ret = pthread_mutexattr_settype(&mutexattr,PTHREAD_MUTEX_ERRORCHECK);
    if (ret != 0) dump_error(ret,"pthread_mutexattr_settype");
    ret = ::pthread_mutex_init(&_object,&mutexattr);
    if (ret != 0) dump_error(ret,"pthread_mutex_init");
    ret = ::pthread_cond_init(&_cond,0);
    if (ret != 0) dump_error(ret,"pthread_cond_init");
  }
  ~CCriticalSection() {
    int ret = ::pthread_mutex_destroy(&_object);
    if (ret != 0) dump_error(ret,"pthread_mutex_destroy");
    ret = ::pthread_cond_destroy(&_cond);
    if (ret != 0) dump_error(ret,"pthread_cond_destroy");
  }
  void Enter() {
    int ret = ::pthread_mutex_lock(&_object);
    if (ret != 0) dump_error(ret,"pthread_mutex_lock");
  }
  void Leave() {
    int ret = ::pthread_mutex_unlock(&_object);
    if (ret != 0) dump_error(ret,"pthread_mutex_unlock");
  }
  void WaitCond() {
    int ret = ::pthread_cond_wait(&_cond, &_object);
    if (ret != 0) dump_error(ret,"pthread_cond_wait");
  }
  void SignalCond() {
    int ret = ::pthread_cond_broadcast(&_cond);
    if (ret != 0) dump_error(ret,"pthread_cond_broadcast");
  }
};
#else
class CCriticalSection
{
  pthread_mutex_t _object;
  pthread_cond_t _cond;
public:
  CCriticalSection() {
    ::pthread_mutex_init(&_object,0);
    ::pthread_cond_init(&_cond,0);
  }
  ~CCriticalSection() {
    ::pthread_mutex_destroy(&_object);
    ::pthread_cond_destroy(&_cond);
  }
  void Enter() { ::pthread_mutex_lock(&_object); }
  void Leave() { ::pthread_mutex_unlock(&_object); }
  void WaitCond() { ::pthread_cond_wait(&_cond, &_object); }
  void SignalCond() { ::pthread_cond_broadcast(&_cond); }
};
#endif
#endif

class CSemaphore : public CBaseHandle
{
public:
  CSemaphore() : CBaseHandle(CBaseHandle::SEMAPHORE) {}
  bool Create(LONG initiallyCount, LONG maxCount)
  {
    if ((initiallyCount < 0) || (initiallyCount > maxCount) || (maxCount < 1)) return false;
    this->u.sema.count    = initiallyCount;
    this->u.sema.maxCount = maxCount;
    return true;
  }
  bool Release(LONG releaseCount = 1);
  bool Lock(DWORD timeoutInterval = INFINITE)
  {
    const HANDLE handles[1] = { this };
    return (::WaitForMultipleObjects (1, handles, FALSE, timeoutInterval) == WAIT_OBJECT_0);
  }
};

class CCriticalSectionLock
{
  CCriticalSection &_object;
  void Unlock()  { _object.Leave(); }
public:
  CCriticalSectionLock(CCriticalSection &object): _object(object)
    {_object.Enter(); }
  ~CCriticalSectionLock() { Unlock(); }
};

}}

#endif

#endif
