#ifndef	COLUMN_H
#define	COLUMN_H

#include "Constraint.h"

#include <List.h>


namespace BALM {

class BALMLayout;
class XTab;

/**
 * Represents a column defined by two x-tabs.
 */
class Column {
	
public:
	XTab*				Left() const;
	XTab*				Right() const;
	Column*				Previous() const;
	void					SetPrevious(Column* value);
	Column*				Next() const;
	void					SetNext(Column* value);
	//~ string				ToString();
	void					InsertBefore(Column* column);
	void					InsertAfter(Column* column);
	Constraint*			HasSameWidthAs(Column* column);
	BList*				Constraints() const;
	void					SetConstraints(BList* constraints);
						~Column();

protected:
						Column(BALMLayout* ls);

protected:
	BALMLayout*			fLS;
	XTab*				fLeft;
	XTab*				fRight;

private:
	Column*				fPrevious;
	Column*				fNext;
	Constraint*			fPreviousGlue;
	Constraint*			fNextGlue;
	BList*				fConstraints;

public:
	friend class			BALMLayout;
	
};

}	// namespace BALM

using BALM::Column;

#endif	// COLUMN_H
