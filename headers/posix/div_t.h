#ifndef _DIV_T_H_
#define _DIV_T_H_
/* 
** Distributed under the terms of the OpenBeOS License.
*/

typedef struct {
	int	quot;
	int	rem;
} div_t;

typedef struct {
	long quot;
	long rem;
} ldiv_t;

typedef struct {
	long long quot;
	long long rem;
} lldiv_t;

#endif /* _DIV_T_H_ */
