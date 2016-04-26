/*  -*- c++ -*-
 * $HeadURL: svn://192.168.2.10/lqn/trunk-V5/lqns/unit-test/test3ehv.cc $
 *
 * Pg 264, Lazowska test.
 * FCFS with class dependent average service times.  Use HV server rather
 * than straight FCFS server.
 *
 *
 * ------------------------------------------------------------------------
 * $Id: test3ehv.cc 8841 2009-07-14 14:21:57Z greg $
 * ------------------------------------------------------------------------
 */

#include <cmath>
#include <stdlib.h>
#include "testmva.h"
#include "server.h"
#include "mva.h"
#include "pop.h"
#include "mva.h"


#define S5K_EQ_HALF	0
#define S5K_EQ_2	1
#define S5K_EQ_8	2
#define S5K_EQ_32	3
#define S5K_EQ_128	4
#define S5K_COUNT	5

#define N_STATIONS	6

void
test( PopVector& NCust, Vector<Server *>& Q, VectorMath<double>& Z, VectorMath<unsigned>& priority, const unsigned s5k_ix)
{
    const unsigned classes  = 5;
    const unsigned stations = N_STATIONS;
    
    NCust.grow(classes);			/* Population vector.		*/
    Z.grow(classes);				/* Think times.			*/
    priority.grow(classes);
    Q.grow(stations);				/* Queue type.  SS/delay.	*/

    Q[1] = new FCFS_Server(classes);		/* Disk1 */
    Q[2] = new FCFS_Server(classes);		/* Disk2 */
    Q[3] = new FCFS_Server(classes);		/* Disk3 */
    Q[4] = new FCFS_Server(classes);		/* Disk4 */
    Q[5] = new HVFCFS_Server(4,classes);	/* CPU */
    Q[6] = new Infinite_Server(classes);	/* Terminals */

    NCust[1] = 2;   Z[1] = 0.0;
    NCust[2] = 3;   Z[2] = 0.0;
    NCust[3] = 3;   Z[3] = 0.0;
    NCust[4] = 2;   Z[4] = 0.0;
    NCust[5] = 6;   Z[5] = 0.0;

    unsigned k;

    for ( unsigned j = 1; j <= 4; ++j ) {
	for ( k = 1; k <= 4; ++k ) {
	    Q[j]->setService(k,1.0).setVisits(k,2.0);
	}
	Q[j]->setService(5,1.0).setVisits(5,(double)j*2.0);
    }

    /* CPU */

    double service;

    switch ( s5k_ix ) {
    case S5K_EQ_HALF:
	service = 2.0;
	break;
    case S5K_EQ_2:
	service = 0.5;
	break;
    case S5K_EQ_8:
	service = 0.125;
	break;
    case S5K_EQ_32:
	service = (1.0/32.0);
	break;
    case S5K_EQ_128:
	service = (1.0/128.0);
	break;
    default:
	cerr << "Invalid S5K index (0-4):" << s5k_ix << endl;
	exit( 1 );
    }

    for ( unsigned e = 1; e <= 4; ++e ) {
	Q[5]->setService(e,e,1,service).setVisits(e,e,1,8.0).setVariance(e,e,1,service * service);
    }

    Q[5]->setService(5,2.0).setVisits(5,20.0).setVariance(1,5,1,4.0);
	
    /* Terminal */

    for ( k = 1; k <= 4; ++k ) {
	Q[6]->setService(k,10.0).setVisits(k,1.0);
    }
    Q[6]->setService(5,0.0).setVisits(5,0.0);
}



void
special_check( ostream& output, const MVA& solver, const unsigned )
{
    const unsigned m = 6;
    const unsigned k = 1;

    output << "Terminal Response Time (by sum of R()) = " << solver.responseTime(  *solver.Q[m], k ) << endl;

    const double response_time = solver.NCust[k] / solver.throughput( m, k ) - solver.Q[m]->S(k);
    output << "Terminal Response Time (by throughput) = " << response_time << endl;
}


static double goodL[S5K_COUNT][4][7][6] = 
{
    {
	{ {0,0,0,0,0,0}, {0, 0.01698, 0.02547, 0.02547, 0.01698, 0.02136}, {0, 0.01737, 0.02606, 0.02606, 0.01737, 0.04355}, {0, 0.01779, 0.02668, 0.02668, 0.01779, 0.06661}, {0, 0.01822, 0.02733, 0.02733, 0.01822, 0.0906},  {0,  1.853,   2.779,   2.779,   1.853,   5.778}, {0, 0.0769,  0.1154,  0.1154,  0.0769,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.01697, 0.02546, 0.02546, 0.01697, 0.02135}, {0, 0.01737, 0.02605, 0.02605, 0.01737, 0.04353}, {0, 0.01778, 0.02667, 0.02667, 0.01778, 0.06658}, {0, 0.01821, 0.02731, 0.02731, 0.01821, 0.09055}, {0,  1.853,   2.779,   2.779,   1.853,   5.778}, {0, 0.0769,  0.1153,  0.1153,  0.0769,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.01697, 0.02546, 0.02546, 0.01697, 0.02135}, {0, 0.01737, 0.02605, 0.02605, 0.01737, 0.04353}, {0, 0.01778, 0.02667, 0.02667, 0.01778, 0.06658}, {0, 0.01821, 0.02731, 0.02731, 0.01821, 0.09055}, {0,  1.853,   2.779,   2.779,   1.853,   5.778}, {0, 0.0769,  0.1153,  0.1153,  0.0769,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.01681, 0.02522, 0.02522, 0.01681, 0.02112}, {0, 0.01718, 0.02577, 0.02577, 0.01718, 0.04301}, {0, 0.01756, 0.02634, 0.02634, 0.01756, 0.06573}, {0, 0.01796, 0.02693, 0.02693, 0.01796, 0.08934}, {0,  1.854,   2.781,   2.781,   1.854,   5.781}, {0, 0.07663, 0.1149,  0.1149,  0.07663, 0.0} }
    },{
	{ {0,0,0,0,0,0}, {0, 0.0335,  0.05025, 0.05025, 0.0335,  0.04471}, {0, 0.0351,  0.05264, 0.05264, 0.0351,  0.09352}, {0, 0.03685, 0.05527, 0.05527, 0.03685,  0.147}, {0, 0.03878, 0.05817, 0.05817, 0.03878, 0.2058}, {0,  1.716,   2.574,   2.574,   1.716,   5.509}, {0, 0.1398,  0.2096,  0.2096,  0.1398,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.03347, 0.05021, 0.05021, 0.03347, 0.04459}, {0, 0.03506, 0.05258, 0.05258, 0.03506, 0.09315}, {0, 0.03679, 0.05518, 0.05518, 0.03679, 0.1462}, {0, 0.03868, 0.05802, 0.05802, 0.03868, 0.2043}, {0,  1.716,   2.574,   2.574,   1.716,   5.512}, {0, 0.1397,  0.2096,  0.2096,  0.1397,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.03347, 0.05021, 0.05021, 0.03347, 0.04459}, {0, 0.03506, 0.05258, 0.05258, 0.03506, 0.09315}, {0, 0.03679, 0.05518, 0.05518, 0.03679, 0.1462}, {0, 0.03868, 0.05802, 0.05802, 0.03868, 0.2043}, {0,  1.716,   2.574,   2.574,   1.716,   5.512}, {0, 0.1397,  0.2096,  0.2096,  0.1397,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.03318, 0.04977, 0.04977, 0.03318, 0.04286}, {0, 0.03465, 0.05198, 0.05198, 0.03465, 0.08899}, {0, 0.03624, 0.05436, 0.05436, 0.03624, 0.1388}, {0, 0.03796, 0.05695, 0.05695, 0.03796, 0.1927}, {0,  1.719,   2.578,   2.578,   1.719,   5.537}, {0, 0.1392,  0.2087,  0.2087,  0.1392,  0.0} }
    },{
	{ {0,0,0,0,0,0}, {0, 0.04391, 0.06587, 0.06587, 0.04391, 0.06129}, {0, 0.04673, 0.0701,  0.0701,  0.04673,  0.131}, {0, 0.04996, 0.07495, 0.07495, 0.04996, 0.2112}, {0, 0.0537,  0.08056, 0.08056, 0.0537,  0.3045}, {0,  1.631,   2.446,   2.446,   1.631,   5.292}, {0, 0.1749,  0.2623,  0.2623,  0.1749,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04381, 0.06572, 0.06572, 0.04381, 0.06058}, {0, 0.04659, 0.06988, 0.06988, 0.04659, 0.1289}, {0, 0.04973, 0.0746,  0.0746,  0.04973, 0.2066}, {0, 0.05333, 0.07999, 0.07999, 0.05333, 0.2954}, {0,  1.632,   2.448,   2.448,   1.632,   5.309}, {0, 0.1746,  0.2619,  0.2619,  0.1746,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04381, 0.06572, 0.06572, 0.04381, 0.06058}, {0, 0.04659, 0.06988, 0.06988, 0.04659, 0.1289}, {0, 0.04973, 0.0746,  0.0746,  0.04973, 0.2066}, {0, 0.05333, 0.07999, 0.07999, 0.05333, 0.2954}, {0,  1.632,   2.448,   2.448,   1.632,   5.309}, {0, 0.1746,  0.2619,  0.2619,  0.1746,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04348, 0.06521, 0.06521, 0.04348, 0.05673}, {0, 0.04605, 0.06908, 0.06908, 0.04605, 0.1193}, {0, 0.04891, 0.07337, 0.07337, 0.04891, 0.1887}, {0, 0.05211, 0.07817, 0.07817, 0.05211, 0.2661}, {0,  1.636,   2.454,   2.454,   1.636,   5.369}, {0, 0.1736,  0.2603,  0.2603,  0.1736,  0.0} }
    },{
	{ {0,0,0,0,0,0}, {0, 0.04759, 0.07139, 0.07139, 0.04759, 0.06759}, {0, 0.05094, 0.07642, 0.07642, 0.05094, 0.1458}, {0, 0.05484, 0.08226, 0.08226, 0.05484, 0.2374}, {0, 0.05944, 0.08916, 0.08916, 0.05944, 0.3465}, {0,  1.601,   2.401,   2.401,   1.601,   5.203}, {0, 0.1866,  0.2799,  0.2799,  0.1866,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04744, 0.07116, 0.07116, 0.04744, 0.06641}, {0, 0.05072, 0.07608, 0.07608, 0.05072, 0.1423}, {0, 0.05448, 0.08173, 0.08173, 0.05448, 0.2299}, {0, 0.05885, 0.08827, 0.08827, 0.05885, 0.3316}, {0,  1.602,   2.403,   2.403,   1.602,    5.23}, {0, 0.1862,  0.2793,  0.2793,  0.1862,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04744, 0.07116, 0.07116, 0.04744, 0.06641}, {0, 0.05072, 0.07608, 0.07608, 0.05072, 0.1423}, {0, 0.05448, 0.08173, 0.08173, 0.05448, 0.2299}, {0, 0.05885, 0.08827, 0.08827, 0.05885, 0.3316}, {0,  1.602,   2.403,   2.403,   1.602,    5.23}, {0, 0.1862,  0.2793,  0.2793,  0.1862,  0.0} },
	{ {0,0,0,0,0,0}, {0, 0.04706, 0.0706,  0.0706,  0.04706, 0.06156}, {0, 0.05011, 0.07516, 0.07516, 0.05011,   0.13}, {0, 0.05352, 0.08027, 0.08027, 0.05352, 0.2067}, {0, 0.05738, 0.08606, 0.08606, 0.05738, 0.2932}, {0,  1.607,   2.411,   2.411,   1.607,   5.308}, {0, 0.1848,  0.2772,  0.2772,  0.1848,  0.0} }
    },{
	{ {0,0,0,0,0,0}, {0, 0.04861, 0.07292, 0.07292, 0.04861, 0.06937}, {0, 0.05212, 0.07818, 0.07818, 0.05212, 0.1499}, {0, 0.05621, 0.08432, 0.08432, 0.05621, 0.2449}, {0, 0.06106, 0.0916,  0.0916,  0.06106, 0.3587}, {0,  1.592,   2.388,   2.388,   1.592,   5.177}, {0, 0.1898,  0.2847,  0.2847,  0.1898,  0.0} }, 
	{ {0,0,0,0,0,0}, {0, 0.04844, 0.07266, 0.07266, 0.04844, 0.06805}, {0, 0.05187, 0.0778,  0.0778,  0.05187, 0.1461}, {0, 0.05582, 0.08373, 0.08373, 0.05582, 0.2365}, {0, 0.06041, 0.09062, 0.09062, 0.06041, 0.3421}, {0,  1.594,   2.391,   2.391,   1.594,   5.207}, {0, 0.1893,   0.284,   0.284,  0.1893,  0.0} }, 
	{ {0,0,0,0,0,0}, {0, 0.04844, 0.07266, 0.07266, 0.04844, 0.06805}, {0, 0.05187, 0.0778,  0.0778,  0.05187, 0.1461}, {0, 0.05582, 0.08373, 0.08373, 0.05582, 0.2365}, {0, 0.06041, 0.09062, 0.09062, 0.06041, 0.3421}, {0,  1.594,   2.391,   2.391,   1.594,   5.207}, {0, 0.1893,   0.284,   0.284,  0.1893,  0.0} }, 
	{ {0,0,0,0,0,0}, {0, 0.04805, 0.07207, 0.07207, 0.04805, 0.06288}, {0, 0.05123, 0.07684, 0.07684, 0.05123,  0.133}, {0, 0.0548,  0.0822,  0.0822,  0.0548,  0.2117}, {0, 0.05885, 0.08828, 0.08828, 0.05885, 0.3008}, {0,  1.599,   2.399,   2.399,   1.599,   5.292}, {0, 0.1878,  0.2817,  0.2817,  0.1878,  0.0} }
    }
};

bool
check( const int solverId, const MVA & solver, const unsigned s5k_ix )
{
    bool ok = true;

    unsigned n = solver.offset(solver.NCust);
    for ( unsigned m = 1; m <= solver.M; ++m ) {
	for ( unsigned k = 1; k <= solver.K; ++k ) {
	    unsigned e;
			
	    if ( m == 5 && k <= 4 ) {
		e = k;
	    } else {
		e = 1;
	    }
	    if ( fabs( solver.L[n][m][e][k] - goodL[s5k_ix][solverId][m][k] ) >= 0.001 ) {
		cerr << "Mismatch at m=" << m <<", k=" << k;
		cerr << ".  Computed=" << solver.L[n][m][e][k] << ", Correct= " << goodL[s5k_ix][solverId][m][k] << endl;
		ok = false;
	    }
	}
    }
    return ok;
}

