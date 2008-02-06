#ifndef	RESULT_TYPE_H
#define	RESULT_TYPE_H


namespace LinearProgramming {
	
/**
 * The possible results of a solving attempt.
 */
enum ResultType {
	NOMEMORY = -2, ERROR = -1, OPTIMAL = 0, SUBOPTIMAL = 1, INFEASIBLE = 2, UNBOUNDED = 3,
        DEGENERATE = 4, NUMFAILURE = 5, USERABORT = 6, TIMEOUT = 7, PRESOLVED = 9, PROCFAIL = 10,
        PROCBREAK = 11, FEASFOUND = 12, NOFEASFOUND = 13
};

}	// namespace LinearProgramming

using LinearProgramming::ResultType;

#endif
