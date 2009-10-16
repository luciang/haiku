/*
 * Copyright 2007-2008, Christof Lutteroth, lutteroth@cs.auckland.ac.nz
 * Copyright 2007-2008, James Kim, jkim202@ec.auckland.ac.nz
 * Distributed under the terms of the MIT License.
 */

#include "Summand.h"
#include "Variable.h"
#include "LinearSpec.h"


/**
 * Gets the summmand's coefficient.
 * 
 * @return the summand's coefficient
 */
double
Summand::Coeff()
{
	return fCoeff;
}


/**
 * Sets the summmand's coefficient.
 * 
 * @param coeff	coefficient
 */
void
Summand::SetCoeff(double coeff)
{
	fCoeff = coeff;
}


/**
 * Gets the summand's variable.
 * 
 * @return the summand's variable
 */
Variable*
Summand::Var()
{
	return fVar;
}


/**
 * Sets the summand's variable.
 * 
 * @param var	variable
 */
void
Summand::SetVar(Variable* var)
{
	fVar = var;
}


/**
 * Destructor.
 */
Summand::~Summand()
{
  fVar->fUsingSummands->RemoveItem(this);
}


/**
 * Constructor.
 */
Summand::Summand(double coeff, Variable* var)
{
	fCoeff = coeff;
	fVar = var;
	fUsedInPenaltyFunction = false;
	fVar->fUsingSummands->AddItem(this);
	
}

