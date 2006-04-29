/* -*- C++ -*-

   This file implements the Job class.

   $ Author: Mirko Boehm $
   $ Copyright: (C) 2004, 2005 Mirko Boehm $
   $ Contact: mirko@kde.org
         http://www.kde.org
         http://www.hackerbuero.org $
   $ License: LGPL with the following explicit clarification:
         This code may be linked against any version of the Qt toolkit
         from Troll Tech, Norway. $

   $Id: Job.cpp 20 2005-08-08 21:02:51Z mirko $
*/

#include <QMutex>
#include <QObject>
#include <QWaitCondition>

#include <DebuggingAids.h>
#include <Thread.h>

#include "Job.h"

namespace ThreadWeaver {

    typedef QMultiMap<Job*, Job*> JobMultiMap;
    Q_GLOBAL_STATIC(JobMultiMap, g_sm_dep)

    QMutex *Job::sm_mutex;

    Job::Job ( QObject *parent )
        : QObject (parent),
	  m_thread (0),
	  m_mutex (new QMutex (QMutex::NonRecursive) ),
          m_finished (false)
    {
        // initialize the process global mutex that protects the dependency tracker:
	if (sm_mutex == 0)
	{
	    sm_mutex=new QMutex();
	}
    }

    Job::~Job()
    {
        resolveDependencies();
    }

    QMultiMap<Job*, Job*> *Job::sm_dep()
    {
        return g_sm_dep();
    }

    class JobRunHelper : public QObject
    {
        Q_OBJECT
    public:
        JobRunHelper()
            : QObject ( 0 )
        {
        }

    signals:
        void started ( Job* );
        void done ( Job* );

    public:

        void runTheJob ( Thread* th, Job* job )
        {
            P_ASSERT ( th == thread() );
            job->m_mutex->lock();
            job->m_thread = th;
            job->m_mutex->unlock();

            emit ( started ( job ) );

            job->run();

            job->m_mutex->lock();
            job->m_thread = 0;
            job->setFinished (true);
            job->m_mutex->unlock();
            job->resolveDependencies(); // notify dependents

            emit ( done( job ) );
        }
    };

    void Job::execute(Thread *th)
    {
	P_ASSERT (sm_dep()->values(this).isEmpty());
        JobRunHelper helper;
        connect ( &helper,  SIGNAL ( started ( Job* ) ),
                  SIGNAL ( started ( Job* ) ) );
        connect ( &helper,  SIGNAL ( done ( Job* ) ),
                  SIGNAL ( done ( Job* ) ) );

	debug(3, "Job::execute: executing job in thread %i.\n", th->id());
        helper.runTheJob( th, this );
	debug(3, "Job::execute: finished execution of job in thread %i.\n", th->id());
    }

    void Job::addDependency (Job *dep)
    {   // if *this* depends on dep, *this* will be the key and dep the value:
	QMutexLocker l(sm_mutex);
	sm_dep()->insert( this, dep );
    }

    bool Job::removeDependency (Job* dep)
    {
	QMutexLocker l(sm_mutex);
	// there may be only one (!) occurence of [this, dep]:
	QMutableMapIterator<Job*, Job*> it(*sm_dep());
	while ( it.hasNext() )
	{
	    it.next();
	    if ( it.key()==this && it.value()==dep )
	    {
		it.remove();
		return true;
	    }
	}
	return false;
    }

    bool Job::hasUnresolvedDependencies ()
    {
        QMutexLocker l(sm_mutex);
        return sm_dep()->contains(this);
   }

    void Job::resolveDependencies ()
    {
        QMutexLocker l(sm_mutex);
        QMutableMapIterator<Job*, Job*> it(*sm_dep());
        // there has to be a better way to do this: (?)
        while ( it.hasNext() )
        {   // we remove all entries where jobs depend on *this* :
            it.next();
            if ( it.value()==this )
            {
                it.remove();
            }
        }
    }

    void Job::aboutToBeQueued ( WeaverInterface* )
    {
    }

}

#include "Job.moc"
#ifdef USE_CMAKE
#include "Job_moc.cpp"
#endif
