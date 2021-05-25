/*  -*- c++ -*-
 * $Id: call.cc 14690 2021-05-24 19:33:27Z greg $
 *
 * Everything you wanted to know about a call to an entry, but were afraid to ask.
 *
 * Copyright the Real-Time and Distributed Systems Group,
 * Department of Systems and Computer Engineering,
 * Carleton University, Ottawa, Ontario, Canada. K1S 5B6
 *
 * November, 1994
 *
 * ------------------------------------------------------------------------
 */


#include "dim.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <mva/server.h>
#include <mva/fpgoop.h>
#include "call.h"
#include "entry.h"
#include "task.h"
#include "submodel.h"
#include "activity.h"
#include "errmsg.h"
#include "lqns.h"

/*----------------------------------------------------------------------*/
/*      Input processing.  Called from model.cc::prepareModel()         */
/*----------------------------------------------------------------------*/

void
Call::Create::operator()( const LQIO::DOM::Call * call )
{
    _src->add_call( _p, call );
}


std::set<Task *>& Call::add_client( std::set<Task *>& clients, const Call * call )
{
    if ( !call->hasForwarding() && call->srcTask()->isUsed() ) {
	clients.insert(const_cast<Task *>(call->srcTask()));
    }
    return clients;
}

std::set<Entity *>& Call::add_server( std::set<Entity *>& servers, const Call * call )
{
    servers.insert(const_cast<Entity *>(call->dstTask()));
    return servers;
}

/*----------------------------------------------------------------------*/
/*                            Generic  Calls                            */
/*----------------------------------------------------------------------*/

/*
 * Initialize and zero fields.   Reverse links are set here.  Forward
 * links are done by subclass.  Processor calls are linked specially.
 */

Call::Call( const Phase * fromPhase, const Entry * toEntry )
    : _dom(nullptr),
      _source(fromPhase),
      _destination(toEntry), 
      _chainNumber(0),
      _replica_number(1),
      _wait(0.0)
{
    if ( toEntry != nullptr ) {
	const_cast<Entry *>(_destination)->addDstCall( this );	/* Set reverse link	*/
    }
}


Call::Call( const Call& src, unsigned int replica )
    : _dom(src._dom),
      _source(nullptr),
      _destination(nullptr),
      _chainNumber(src._chainNumber),
      _replica_number(replica),
      _wait(0.0)
{
}

	   
/*
 * Clean up the mess.
 */

Call::~Call()
{
}


int
Call::operator==( const Call& item ) const
{
    return (dstEntry() == item.dstEntry());
}


int
Call::operator!=( const Call& item ) const
{
    return (dstEntry() != item.dstEntry());
}


bool
Call::check() const
{
    const int srcReplicas = srcTask()->replicas();
    const int dstReplicas = dstTask()->replicas();
    if ( srcReplicas > 1 || dstReplicas > 1 ) {
	const int fanOut = srcTask()->fanOut( dstTask() );
	const int fanIn  = dstTask()->fanIn( srcTask() );
	if ( fanIn == 0 || fanOut == 0 || srcReplicas * fanOut != dstReplicas * fanIn ) {
	    const std::string& srcName = srcTask()->name();
	    const std::string& dstName = dstTask()->name();
	    LQIO::solution_error( ERR_REPLICATION, 
				  fanOut, srcName.c_str(), srcReplicas,
				  fanIn,  dstName.c_str(), dstReplicas );
	    return false;
	}
    }
    return true;
}


double
Call::getDOMValue() const
{
    const double value = getDOM()->getCallMeanValue();
    if ( (getDOM()->getCallType() != LQIO::DOM::Call::Type::FORWARD && getSource()->phaseTypeFlag() == LQIO::DOM::Phase::Type::DETERMINISTIC && value != std::floor( value ))
	 || getDOM()->getCallType() == LQIO::DOM::Call::Type::FORWARD && value > 1.0 ) {
	std::stringstream ss;
	ss << value << " < " << value;
	throw std::domain_error( ss.str() );
    }
    return value;
}



unsigned
Call::fanIn() const
{
    return dstTask()->fanIn( srcTask() );
}


unsigned
Call::fanOut() const
{
    return srcTask()->fanOut( dstTask() );
}


/*
 * Return the name of the destination entry.
 */

const std::string&
Call::dstName() const
{
    return dstEntry()->name();
}



/*
 * Return the submodel number.
 */

unsigned
Call::submodel() const
{
    return dstTask()->submodel();
}


bool
Call::hasOvertaking() const
{
    return hasRendezvous() && dstEntry()->maxPhase() > 1;
}



/*
 * Return the total wait along this arc.  Take into account
 * replication.  This applies for both Pan and Bug 299 replication.
 */

double
Call::rendezvousDelay() const
{
    if ( hasRendezvous() ) {
	return rendezvous() * wait() * fanOut();
    } else {
	return 0.0;
    }
}


#if PAN_REPLICATION
/*
 * Compute and save old rendezvous delay.		// REPL
 */

double
Call::rendezvousDelay( const unsigned k )
{
    if ( dstTask()->hasServerChain(k) ) {
	return rendezvous() * wait() * (fanOut() - 1);
    } else {
	return Call::rendezvousDelay();	// rendezvousDelay is already multiplied by fanOut.
    }
}
#endif


/*
 * Return the name of the source of this call.  Sources are always tasks.
 */

const std::string&
Call::srcName() const
{
    return getSource()->name();
}


/*
 * Return the source task of this call.  Sources are always tasks.
 */

const Task *
Call::srcTask() const
{
    return dynamic_cast<const Task *>(getSource()->owner());
}



double
Call::elapsedTime() const
{
    if (flags.trace_quorum) {
	std::cout <<"\nCall::elapsedTime(): call " << this->srcName() << " to " << dstEntry()->name() << std::endl;
    }

    if ( hasRendezvous() ) {
	return dstEntry()->elapsedTimeForPhase(1);
    } else {
	return 0.0;
    }
}



/*
 * Return time spent in the queue for call to this entry.
 */

double
Call::queueingTime() const
{
    if ( hasRendezvous() ) {
	if ( std::isinf( _wait ) ) return _wait;
	const double q = _wait - elapsedTime();
	if ( q <= 0.000001 ) {
	    return 0.0;
	} else if ( q * elapsedTime() > 0. && (q/elapsedTime()) <= 0.0001 ) {
	    return 0.0;
	} else {
	    return q;
	}
    } else if ( hasSendNoReply() ) {
	return _wait;
    } else {
	return 0.0;
    }
}


const Call&
Call::insertDOMResults() const
{
    const_cast<LQIO::DOM::Call *>(getDOM())->setResultWaitingTime(queueingTime());
    return *this;
}


#if PAN_REPLICATION
/*
 * Return the adjustment factor for this call.  //REPL
 */

double
Call::nrFactor( const Submodel& aSubmodel, const unsigned k ) const
{
    const Entity * dst_task = dstTask();
    return aSubmodel.nrFactor( dst_task->serverStation(), index(), k ) * fanOut() * rendezvous();	// 5.20
}
#endif



/*
 * Return variance of this arc.
 */

double
Call::variance() const
{
    if ( hasRendezvous() ) {
	return dstEntry()->varianceForPhase(1) + square(queueingTime());
    } else {
	return 0.0;
    }
}



/*
 * Return the coefficient of variation for this particular call.
 */

double
Call::CV_sqr() const
{
#ifdef NOTDEF
    return dstEntry()->variance(1) / square(elapsedTime());
#endif
    return variance() / square(wait());
}



/*
 * Follow the call to its destination.
 */

const Call&
Call::followInterlock( Interlock::CollectTable& path ) const
{
    /* Mark current */

    if ( rendezvous() > 0.0 && !path.prune() ) {
	Interlock::CollectTable branch( path, path.calls() * rendezvous() );
	const_cast<Entry *>(dstEntry())->initInterlock( branch );
    }
    return *this;
}



/*
 * Set the visit ratio at the destinations station.
 */

void
Call::setVisits( const unsigned k, const unsigned p, const double rate )
{
    const Entity * aServer = dstTask();
    if ( aServer->hasServerChain( k ) && hasRendezvous() && !srcTask()->hasInfinitePopulation() ) {
	Server * aStation = aServer->serverStation();
	const unsigned e = dstEntry()->index();
#if BUG_299
	aStation->addVisits( e, k, p, rendezvous() / fanOut() * rate );
#else
	aStation->addVisits( e, k, p, rendezvous() * rate );
#endif
    }
}


//tomari: set the chain number associated with this call.
void
Call::setChain( const unsigned k, const unsigned p, const double rate )
{
    const Entity * aServer = dstTask();
    if ( aServer->hasServerChain( k )  ){

	_chainNumber = k;

	if ( flags.trace_replication ) {
	    std::cout <<"\nCall::setChain, k=" << k<< "  " ;
	    std::cout <<",call from "<< srcName() << " To " << dstName()<< std::endl;
	}
    }
}




/*
 * Set the open arrival rate to the destination's station.
 */

void
Call::setLambda( const unsigned, const unsigned p, const double rate )
{
    Server * aStation = dstTask()->serverStation();
    const unsigned e = dstEntry()->index();
    if ( hasSendNoReply() ) {
	aStation->addVisits( e, 0, p, getSource()->throughput() * sendNoReply() );
    } else if ( hasRendezvous() && srcTask()->isInOpenModel() && srcTask()->isInfinite() ) {
	aStation->addVisits( e, 0, p, getSource()->throughput() * rendezvous() );
    }
}


/*
 * Clear waiting time.
 */

void
Call::clearWait( const unsigned k, const unsigned p, const double )
{
    _wait = 0.0;
}



/*
 * Get the waiting time for this call from the mva submodel.  A call
 * can potentially orginate from multiple chains, so add them all up.
 * (Call clearWait first.)
 */

void
Call::saveOpen( const unsigned, const unsigned p, const double )
{
    const unsigned e = dstEntry()->index();
    const Server * aStation = dstTask()->serverStation();

    if ( aStation->V( e, 0, p ) > 0.0 ) {
	_wait = aStation->W[e][0][p];
    }
}



/*
 * Get the waiting time for this call from the mva submodel.  A call
 * can potentially orginate from multiple chains, so add them all up.
 * (Call clearWait first.).  This may havve to be changed if the
 * result varies by chain.  Priorities perhaps?
 */

void
Call::saveWait( const unsigned k, const unsigned p, const double )
{
    const Entity * aServer = dstTask();
    const unsigned e = dstEntry()->index();
    const Server * aStation = aServer->serverStation();

    if ( aStation->V( e, k, p ) > 0.0 ) {
	_wait = aStation->W[e][k][p];
    }
}

/*----------------------------------------------------------------------*/
/*                              Phase Calls                             */
/*----------------------------------------------------------------------*/

void
NullCall::parameter_error( const std::string& s ) const
{
    abort();
}

void
FromEntry::parameter_error( const std::string& s ) const
{
    LQIO::solution_error( LQIO::ERR_INVALID_CALL_PARAMETER, "entry", srcEntry()->name().c_str(), getSource()->getDOM()->getTypeName(),
			  getSource()->getDOM()->getName().c_str(), dstName().c_str(), s.c_str() );
    throw std::domain_error( s );
}

void
FromActivity::parameter_error( const std::string& s ) const
{
    LQIO::solution_error( LQIO::ERR_INVALID_CALL_PARAMETER, "task", srcTask()->name().c_str(), getSource()->getDOM()->getTypeName(),
			  getSource()->getDOM()->getName().c_str(), dstName().c_str(), s.c_str() );
    throw std::domain_error( s );
}

/*----------------------------------------------------------------------*/
/*                              Phase Calls                             */
/*----------------------------------------------------------------------*/

/*
 * Call from a phase to a task.
 */

PhaseCall::PhaseCall( const Phase * fromPhase, const Entry * toEntry )
    : Call( fromPhase, toEntry ),
      FromEntry( fromPhase->entry() )
{
    const_cast<Phase *>(getSource())->addSrcCall( this );
}


/*
 * Deep copy.  Set FromEntry to the replica.
 */

PhaseCall::PhaseCall( const PhaseCall& src, unsigned int replica )
    : Call( src, replica ),
      FromEntry( Entry::find( src.srcEntry()->name(), src.srcEntry()->getReplicaNumber() ) )
{
    /* Link to source replica */
    const Phase& src_phase = src.srcEntry()->getPhase(src.getSource()->getPhaseNumber());
    setSource( &src_phase );    /* Swap _source to the replica !!! */
    const_cast<Phase&>(src_phase).addSrcCall( this );

    /* Link to destination replica */
    if ( src.dstEntry() != nullptr ) {
	setDestination( Entry::find( src.dstEntry()->name(), static_cast<unsigned>(std::ceil( static_cast<double>(replica) / static_cast<double>(src.fanIn())) ) ) );
    }
}


/*
 * Expand replicas (Not PAN_REPLICATION).  I need to find the phase in
 * the replica entry.  Note that the destination is affected by fan-out too.
 */

Call&
PhaseCall::expand()
{
    const unsigned int replicas = fanOut();
    for ( unsigned int replica = 2; replica <= replicas; ++replica ) {
	PhaseCall * call = clone( replica );
	const_cast<Entity *>(call->dstTask())->addTask( call->srcTask() );
    }
    return *this;
}



/*
 * Initialize waiting time.
 */

Call&
PhaseCall::initWait()
{
    setWait( elapsedTime() );			/* Initialize arc wait. 	*/
    return *this;
}

/*----------------------------------------------------------------------*/
/*                           Forwarded Calls                            */
/*----------------------------------------------------------------------*/

/*
 * call added to transform forwarding to standard model.
 */

ForwardedCall::ForwardedCall( const Phase * fromPhase, const Entry * toEntry, const Call * fwdCall )
    : Call( fromPhase, toEntry ), PhaseCall( fromPhase, toEntry ), _fwdCall( fwdCall )
{
}

bool
ForwardedCall::check() const
{
    const Task * srcTask = dynamic_cast<const Task *>(_fwdCall->getSource()->owner());
    const int srcReplicas = srcTask->replicas();
    const int dstReplicas = dstTask()->replicas();
    if ( srcReplicas > 1 || dstReplicas > 1 ) {
	const int fanOut = srcTask->fanOut( dstTask() );
	const int fanIn  = dstTask()->fanIn( srcTask );
	if ( fanIn == 0 || fanOut == 0 || srcReplicas * fanOut != dstReplicas * fanIn ) {
	    const std::string& srcName = srcTask->name();
	    const std::string& dstName = dstTask()->name();
	    LQIO::solution_error( ERR_REPLICATION, 
				  fanOut, srcName.c_str(), srcReplicas,
				  fanIn,  dstName.c_str(), dstReplicas );
	    return false;
	}
    }
    return true;
}

const std::string&
ForwardedCall::srcName() const
{
    return _fwdCall->srcName();
}


const ForwardedCall&
ForwardedCall::insertDOMResults() const
{
    LQIO::DOM::Call* fwdDOM = const_cast<LQIO::DOM::Call *>(_fwdCall->getDOM());		/* Proxy */
    fwdDOM->setResultWaitingTime(queueingTime());
    return *this;
}

/*----------------------------------------------------------------------*/
/*                           Processor Calls                            */
/*----------------------------------------------------------------------*/

/*
 * Call to processor entry.
 */

ProcessorCall::ProcessorCall( const Phase * fromPhase, const Entry * toEntry )
    : Call( fromPhase, toEntry )
{
}



/*
 * Set up waiting time to processors.
 */

Call&
ProcessorCall::initWait()
{
    setWait( dstEntry()->serviceTimeForPhase(1) );		/* Initialize arc wait. 	*/
    return *this;
}

/*----------------------------------------------------------------------*/
/*                            Activity Calls                            */
/*----------------------------------------------------------------------*/

/*
 * Call added for activities.
 */

ActivityCall::ActivityCall( const Activity * fromActivity, const Entry * toEntry )
    : Call( fromActivity, toEntry ), FromActivity()
{
    const_cast<Phase *>(getSource())->addSrcCall( this );
}


ActivityCall::ActivityCall( const ActivityCall& src, unsigned int replica )
    : Call( src, replica ), FromActivity()
{
//  _destination = Entry::find( src._destination, replica adjusted for fanin/fanout, like task */	
}



/*
 * Expand replicas (Not PAN_REPLICATION).  I need to find the activity
 * in the replica task.
 */

Call&
ActivityCall::expand()
{
    const unsigned int replicas = srcTask()->replicas();	/* Replicas of source */
    for ( unsigned int replica = 2; replica <= replicas; ++replica ) {
//	Task * task = Task::find( srcTask()->name(), replica );
//      Activity * activity = task->findActivity(...);
//	activity->add_call( clone( replica ) );
    }
    return *this;
}



/*
 * Initialize waiting time.
 */

Call&
ActivityCall::initWait()
{
    setWait( elapsedTime() );			/* Initialize arc wait. 	*/
    return *this;
}

/*----------------------------------------------------------------------*/
/*                       Activity Forwarded Calls                       */
/*----------------------------------------------------------------------*/

/*
 * call added to transform forwarding to standard model.
 */

ActivityForwardedCall::ActivityForwardedCall( const Activity * fromActivity, const Entry * toEntry )
    : Call( fromActivity, toEntry ), ActivityCall( fromActivity, toEntry )
{
}

/*----------------------------------------------------------------------*/
/*                        Phase Processor Calls                         */
/*----------------------------------------------------------------------*/

PhaseProcessorCall::PhaseProcessorCall( const Phase * fromPhase, const Entry * toEntry ) 
    : Call( fromPhase, toEntry ), ProcessorCall( fromPhase, toEntry ), FromEntry( fromPhase->entry() )
{
}

/*----------------------------------------------------------------------*/
/*                      Activity Processor Calls                        */
/*----------------------------------------------------------------------*/

ActivityProcessorCall::ActivityProcessorCall( const Activity * fromActivity, const Entry * toEntry )
    : Call( fromActivity, toEntry ), ProcessorCall( fromActivity, toEntry ), FromActivity()
{
}

/*----------------------------------------------------------------------*/
/*                          CallStack Functions                         */
/*----------------------------------------------------------------------*/

/*
 * We are looking for matching tasks for calls.
 */

bool 
Call::Find::operator()( const Call * call ) const
{
    if ( call == nullptr || call->getDOM() == nullptr ) return false;

    if ( call->hasSendNoReply() ) _broken = true;		/* Cycle broken - async call */
    if ( call->dstTask() == _call->dstTask() ) {
	if ( call->hasRendezvous() && _call->hasRendezvous() && !_broken ) {
	    throw call_cycle();		/* Dead lock */
	} else if ( call->dstEntry() == _call->dstEntry() && _direct_path ) {
	    throw call_cycle();		/* Livelock */
	} else {
	    return true;
	}
    }
    return false;
}

/*
 * We may skip back over forwarded calls when computing the size.
 */

unsigned
Call::stack::depth() const	
{
    return std::count_if( begin(), end(), Predicate<Call>( &Call::hasNoForwarding ) );
}
