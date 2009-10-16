/*
 * Copyright 2007-2008, Christof Lutteroth, lutteroth@cs.auckland.ac.nz
 * Copyright 2007-2008, James Kim, jkim202@ec.auckland.ac.nz
 * Distributed under the terms of the MIT License.
 */

#ifndef	CONSTRAINT_H
#define	CONSTRAINT_H

#include "OperatorType.h"
#include "Variable.h"
#include "Summand.h"

#include <File.h>
#include <List.h>
#include <String.h>
#include <SupportDefs.h>
#include <math.h>


namespace LinearProgramming {
	
class LinearSpec;

/**
 * Hard linear constraint, i.e.&nbsp;one that must be satisfied. 
 * May render a specification infeasible.
 */
class Constraint {
	
public:
	int32				Index();

	BList*				LeftSide();
	void				SetLeftSide(BList* summands);
	void				UpdateLeftSide();
	void				SetLeftSide(double coeff1, Variable* var1);
	void				SetLeftSide(double coeff1, Variable* var1, 
							double coeff2, Variable* var2);
	void				SetLeftSide(double coeff1, Variable* var1, 
							double coeff2, Variable* var2,
							double coeff3, Variable* var3);
	void				SetLeftSide(double coeff1, Variable* var1, 
							double coeff2, Variable* var2,
							double coeff3, Variable* var3,
							double coeff4, Variable* var4);

	OperatorType		Op();
	void				SetOp(OperatorType value);
	double				RightSide() const;
	void				SetRightSide(double value);
	double				PenaltyNeg() const;
	void				SetPenaltyNeg(double value);
	double				PenaltyPos() const;
	void				SetPenaltyPos(double value);

	const char*			Label();
	void				SetLabel(const char* label);

	void				WriteXML(BFile* file);

	Variable*			DNeg() const;
	Variable*			DPos() const;

	void				SetOwner(void* owner);
	void*				Owner() const;

	bool				IsValid();
	void				Invalidate();

	BString*			ToBString();
	const char*			ToString();

						~Constraint();

protected:
						Constraint(LinearSpec* ls, BList* summands,
								OperatorType op, double rightSide,
								double penaltyNeg, double penaltyPos);

private:
	LinearSpec*			fLS;
	BList*				fLeftSide;
	OperatorType		fOp;
	double				fRightSide;
	Summand*			fDNegObjSummand;
	Summand*			fDPosObjSummand;
	void*				fOwner;
	char*				fLabel;

	bool				fIsValid;

public:
	friend class		LinearSpec;

};

}	// namespace LinearProgramming

using LinearProgramming::Constraint;

#endif	// CONSTRAINT_H

