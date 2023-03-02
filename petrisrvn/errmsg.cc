/************************************************************************/
/* Copyright the Real-Time and Distributed Systems Group,		*/
/* Department of Systems and Computer Engineering,			*/
/* Carleton University, Ottawa, Ontario, Canada. K1S 5B6		*/
/* 									*/
/* Novemeber 1990.							*/
/* August 1991.								*/
/************************************************************************/

/*
 * $Id: errmsg.cc 16448 2023-02-27 13:04:14Z greg $
 */

#include "petrisrvn.h"
#include <lqio/error.h>
#include <lqio/dom_document.h>
#include "errmsg.h"

/*
 * Error messages.
 */

std::vector< std::pair<unsigned, LQIO::error_message_type> > local_error_messages =
{
    { FTL_TAG_TABLE_FULL,               { LQIO::error_severity::FATAL,      "Tag hash table overflow."} },
    { ERR_SEND_NO_REPLIES_PROHIBITED,   { LQIO::error_severity::ERROR,      "Send-no-reply from \"%s\" to \"%s\" is not supported."} },
    { ERR_BOGUS_REFERENCE_TASK,         { LQIO::error_severity::ERROR,      "Entry \"%s\" for reference task \"%s\" must have service time, think time, or deterministic phases."} },
    { ERR_MULTI_SYNC_SERVER,            { LQIO::error_severity::ERROR,      "Task \"%s\" provides external synchronization: it cannot be a multiserver."} },
    { ERR_COMMON_ENTRY_EXTERNAL_SYNC,   { LQIO::error_severity::ERROR,      "Task \"%s\": join from common entry \"%s\"."} },
    { WRN_CONVERGENCE,                  { LQIO::error_severity::WARNING,    "Convergence problems for \"%s\"; precision is %g."} },
    { WRN_PREEMPTIVE_SCHEDULING,        { LQIO::error_severity::WARNING,    "Premptive scheduling for processor \"%s\" cannot be used with non-unity coefficient of variation at entry \"%s\"."} },
    { ADV_MESSAGES_LOST,                { LQIO::error_severity::ADVISORY,   "Open-class messages are dropped at task \"%s\" with probability %g."} },
    { ADV_OPEN_ARRIVALS_DONT_MATCH,     { LQIO::error_severity::ADVISORY,   "Throughput %g does not match open arrival rate %g at Entry \"%s\"."} },
    { ADV_ERLANG_N,                     { LQIO::error_severity::ADVISORY,   "Using Erlang %d distribution for Entry \"%s\"."} }
};

/*
 * What to do based on the severity of the error.
 */

void
LQIO::severity_action (error_severity severity)
{
    switch( severity ) {
    case LQIO::error_severity::FATAL:
	exit( EXCEPTION_EXIT );
	break;

    case LQIO::error_severity::ERROR:
	LQIO::io_vars.error_count += 1;
	if  ( LQIO::io_vars.error_count >= LQIO::io_vars.max_error ) {
	    throw std::runtime_error( "Too many errors" );
	}
    default:
	break;
    }
}
