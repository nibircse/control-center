#ifndef RT_OS_WINDOWS
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "MRE_Pthread.h"
#include "Locker.h"

int SynchroPrimitives::CPthreadMRE::MRE_Wait(CPthreadMRE *lpMre, int timeInMs) {
  int res = 0;
  timeval tv;
  timespec ts;

  gettimeofday(&tv, NULL);

  ts.tv_sec = time(NULL) + timeInMs / 1000;
  ts.tv_nsec = tv.tv_usec * 1000 + 1000 * 1000 * (timeInMs % 1000);
  ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
  ts.tv_nsec %= (1000 * 1000 * 1000);

  pthread_mutex_lock(&lpMre->m_mreInfo.m_mutex);
  while(!lpMre->m_mreInfo.m_signaled) {
    res = pthread_cond_timedwait(&lpMre->m_mreInfo.m_cond, &lpMre->m_mreInfo.m_mutex, &ts);
    if (res == ETIMEDOUT) break;
  }
  pthread_mutex_unlock(&lpMre->m_mreInfo.m_mutex);

  SynchroPrimitives::Locker lock(&lpMre->m_mreInfo.m_cs);
  lpMre->m_mreInfo.m_signaled = true;
  return res;
}
//////////////////////////////////////////////////////////////////////////

int SynchroPrimitives::CPthreadMRE::MRE_Set(CPthreadMRE *lpMre) {
  SynchroPrimitives::Locker lock(&lpMre->m_mreInfo.m_cs);
  lpMre->m_mreInfo.m_signaled = true;
  int res = pthread_cond_signal(&lpMre->m_mreInfo.m_cond);
  return res;
}
//////////////////////////////////////////////////////////////////////////

int SynchroPrimitives::CPthreadMRE::MRE_Reset(CPthreadMRE *lpMre) {
  SynchroPrimitives::Locker lock(&lpMre->m_mreInfo.m_cs);
  lpMre->m_mreInfo.m_signaled = false;
  return 0;
}
//////////////////////////////////////////////////////////////////////////

int SynchroPrimitives::CPthreadMRE::MRE_Init(CPthreadMRE *lpMre) {
  int result = pthread_mutex_init(&lpMre->m_mreInfo.m_mutex, NULL);
  if (result != 0)
    return result;
  result = pthread_cond_init(&lpMre->m_mreInfo.m_cond, NULL);
  if (result != 0)
    return result;
  lpMre->m_mreInfo.m_signaled = false;
  return result;
}
//////////////////////////////////////////////////////////////////////////

int SynchroPrimitives::CPthreadMRE::MRE_Destroy(CPthreadMRE *lpMre) {
  int res = pthread_mutex_destroy(&lpMre->m_mreInfo.m_mutex);
  if (res != 0) return res;
  res = pthread_cond_destroy(&lpMre->m_mreInfo.m_cond);
  return res;
}
//////////////////////////////////////////////////////////////////////////
#endif //RT_OS_WINDOWS