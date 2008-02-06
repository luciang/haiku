#ifndef	LINEAR_SPEC_H
#define	LINEAR_SPEC_H

#include "OperatorType.h"
#include "ResultType.h"
#include "OptimizationType.h"

#include "lp_lib.h"

#include <List.h>
#include <OS.h>
#include <SupportDefs.h>


namespace LinearProgramming {
	
class Constraint;
class ObjFunctionSummand;
class PenaltyFunction;
class SoftConstraint;
class Variable;

/**
 * Specification of a linear programming problem.
 */
class LinearSpec {
	
public:
						LinearSpec();
						~LinearSpec();
	void					UpdateObjFunction();
	void					SetObjFunction(BList* coeffs, BList* vars);
	ObjFunctionSummand*	AddObjFunctionSummand(double coeff, Variable* var);
	Variable*				AddVariable();

	Constraint*			AddConstraint(BList* coeffs, BList* vars, 
								OperatorType op, double rightSide);
	Constraint*			AddConstraint(double coeff1, Variable* var1, 
								OperatorType op, double rightSide);
	Constraint*			AddConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								OperatorType op, double rightSide);
	Constraint*			AddConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								double coeff3, Variable* var3, 
								OperatorType op, double rightSide);
	Constraint*			AddConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								double coeff3, Variable* var3, 
								double coeff4, Variable* var4, 
								OperatorType op, double rightSide);

	SoftConstraint*		AddSoftConstraint(BList* coeffs, BList* vars, 
								OperatorType op, double rightSide, 
								double penaltyNeg, double penaltyPos);
	SoftConstraint*		AddSoftConstraint(double coeff1, Variable* var1, 
								OperatorType op, double rightSide, 
								double penaltyNeg, double penaltyPos);
	SoftConstraint*		AddSoftConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								OperatorType op, double rightSide, 
								double penaltyNeg, double penaltyPos);
	SoftConstraint*		AddSoftConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								double coeff3, Variable* var3, 
								OperatorType op, double rightSide, 
								double penaltyNeg, double penaltyPos);
	SoftConstraint*		AddSoftConstraint(double coeff1, Variable* var1, 
								double coeff2, Variable* var2, 
								double coeff3, Variable* var3, 
								double coeff4, Variable* var4, 
								OperatorType op, double rightSide, 
								double penaltyNeg, double penaltyPos);
	
	PenaltyFunction*		AddPenaltyFunction(Variable* var, BList* xs, BList* gs);
	void					RemovePresolved();
	ResultType			Presolve();
	ResultType			Solve();
	void					Save(char* fname);

	int32				Columns() const;
	void					SetColumns(int32 value);
	OptimizationType		Optimization() const;
	void					SetOptimization(OptimizationType value);
	lprec*				LP() const;
	void					SetLP(lprec* value);
	BList*				ObjFunctionSummands() const;
	void					SetObjFunctionSummands(BList* value);
	BList*				Variables() const;
	void					SetVariables(BList* value);
	BList*				Constraints() const;
	void					SetConstraints(BList* value);
	ResultType			Result() const;
	void					SetResult(ResultType value);
	double				ObjectiveValue() const;
	void					SetObjectiveValue(double value);
	double				SolvingTime() const;
	void					SetSolvingTime(double value);

protected:
	int32 				fColumns;

private:
	lprec*				fLpPresolved;
	OptimizationType		fOptimization;
	lprec*				fLP;
	BList*				fObjFunctionSummands;
	BList*				fVariables;
	BList*				fConstraints;
	ResultType			fResult;
	double 				fObjectiveValue;
	double 				fSolvingTime;		// = Double.Nan

public:
	friend class			ObjFunctionSummand;
	friend class			SoftConstraint;

};

}	// namespace LinearProgramming

using LinearProgramming::LinearSpec;

#endif	// LINEAR_SPEC_H
