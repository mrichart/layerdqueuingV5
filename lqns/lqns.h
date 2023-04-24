/* -*- c++ -*-
 * $HeadURL: http://rads-svn.sce.carleton.ca:8080/svn/lqn/trunk-V5/lqns/lqns.h $
 *
 * Dimensions common to everything, plus some funky inline functions.
 *
 * Copyright the Real-Time and Distributed Systems Group,
 * Department of Systems and Computer Engineering,
 * Carleton University, Ottawa, Ontario, Canada. K1S 5B6
 *
 * November, 1994
 *
 * $Id: lqns.h 16698 2023-04-24 00:52:30Z greg $
 *
 * ------------------------------------------------------------------------
 */

#ifndef	LQNS_LQNS_H
#define LQNS_LQNS_H

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>


#define MAX_CLASSES     200                     /* Max classes (clients)        */
#define MAX_PHASES      3                       /* Number of Phases.            */
#define N_SEMAPHORE_ENTRIES     2               /* Number of semaphore entries  */

#define	BUG_270		1			/* Enable prune pragma		*/
#define PAN_REPLICATION	1			/* Use Amy Pan's replication	*/
#define BUG_299_PRUNE	1			/* Enable replica prune code	*/

const double EPSILON = 0.000001;		/* For testing against 1 or 0 */

/*
 * Return square.  C++ doesn't even have an exponentiation operator, let
 * alone a smart one.
 */

template <typename Type> inline Type square( Type a ) { return a * a; }
template <typename Type> inline void Delete( Type x ) { delete x; }

/* 
 * Common under-relaxation code.  Adapted to include newton-raphson
 * adjustment.  
 */

double under_relax( const double old_value, const double new_value, const double relax );

template <class Type1, class Type2> struct Exec1
{
    typedef Type1& (Type1::*funcPtr)( Type2 x );
    Exec1<Type1,Type2>( funcPtr f, Type2 x ) : _f(f), _x(x) {}
    void operator()( Type1 * object ) const { (object->*_f)( _x ); }
    void operator()( Type1& object ) const { (object.*_f)( _x ); }
private:
    funcPtr _f;
    Type2 _x;
};

template <class Type1, class Type2, class Type3> struct Exec2
{
    typedef Type1& (Type1::*funcPtr)( Type2 x, Type3 y );
    Exec2<Type1,Type2,Type3>( funcPtr f, Type2 x, Type3 y ) : _f(f), _x(x), _y(y) {}
    void operator()( Type1 * object ) const { (object->*_f)( _x, _y ); }
    void operator()( Type1& object ) const { (object.*_f)( _x, _y ); }
private:
    funcPtr _f;
    Type2 _x;
    Type3 _y;
};

template <class Type1, class Type2, class Type3 > struct ExecSum1
{
    typedef Type2 (Type1::*funcPtr)( Type3 );
    ExecSum1<Type1,Type2,Type3>( funcPtr f, Type3 arg ) : _f(f), _arg(arg), _sum(0.) {}
    void operator()( Type1 * object ) { _sum += (object->*_f)( _arg ); }
    void operator()( Type1& object ) { _sum += (object.*_f)( _arg ); }
    Type2 sum() const { return _sum; }
private:
    const funcPtr _f;
    Type3 _arg;
    Type2 _sum;
};
    
template <class Type1, class Type2, class Type3, class Type4 > struct ExecSum2
{
    typedef Type2 (Type1::*funcPtr)( Type3, Type4 );
    ExecSum2<Type1,Type2,Type3,Type4>( funcPtr f, Type3 arg1, Type4 arg2 ) : _f(f), _arg1(arg1), _arg2(arg2), _sum(0.) {}
    void operator()( Type1 * object ) { _sum += (object->*_f)( _arg1, _arg2 ); }
    void operator()( Type1& object ) { _sum += (object.*_f)( _arg1, _arg2 ); }
    Type2 sum() const { return _sum; }
private:
    const funcPtr _f;
    Type3 _arg1;
    Type4 _arg2;
    Type2 _sum;
};
    
template <class Type1, class Type2> struct ConstExec1
{
    typedef const Type1& (Type1::*funcPtr)( Type2 ) const;
    ConstExec1<Type1,Type2>( const funcPtr f, Type2 x ) : _f(f), _x(x) {}
    void operator()( const Type1 * object ) const { (object->*_f)(_x); }
    void operator()( const Type1& object ) const { (object.*_f)(_x); }
private:
    const funcPtr _f;
    Type2 _x;
};
    
template <class Type1> struct ConstPrint
{
    typedef std::ostream& (Type1::*funcPtr)( std::ostream& ) const;
    ConstPrint<Type1>( const funcPtr f, std::ostream& o ) : _f(f), _o(o) {}
    void operator()( const Type1 * object ) const { (object->*_f)( _o ); }
    void operator()( const Type1& object ) const { (object.*_f)( _o ); }
private:
    const funcPtr _f;
    std::ostream& _o;
};


template <class Type1, class Type2> struct ConstPrint1
{
    typedef std::ostream& (Type1::*funcPtr)( std::ostream&, Type2 ) const;
    ConstPrint1<Type1,Type2>( const funcPtr f, std::ostream& o, Type2 x ) : _f(f), _o(o), _x(x) {}
    void operator()( const Type1 * object ) const { (object->*_f)( _o, _x ); }
    void operator()( const Type1& object ) const { (object.*_f)( _o, _x ); }
private:
    const funcPtr _f;
    std::ostream& _o;
    const Type2 _x;
};


template <class Type> struct EQStr
{
    EQStr( const std::string & s ) : _s(s) {}
    bool operator()(const Type * e1 ) const { return e1->name() == _s; }
private:
    const std::string & _s;
};

template <class Type> struct EqualsReplica {
    EqualsReplica<Type>( const std::string& name, unsigned int replica=1 ) : _name(name), _replica(replica) {}
    bool operator()( const Type * object ) const { return object->name() == _name && object->getReplicaNumber() == _replica; }
private:
    const std::string _name;
    const unsigned int _replica;
};
#endif
