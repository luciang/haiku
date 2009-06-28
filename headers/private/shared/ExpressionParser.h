/*
 * Copyright 2006-2009 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H


#include <String.h>


class Tokenizer;

class ParseException {
 public:
	ParseException(const char* message, int32 position)
		: message(message),
		  position(position)
	{
	}

	ParseException(const ParseException& other)
		: message(other.message),
		  position(other.position)
	{
	}

	BString	message;
	int32	position;
};

struct Function;
struct Token;
class MAPM;

class ExpressionParser {
 public:
								ExpressionParser();
								~ExpressionParser();

			void				SetSupportHexInput(bool enabled);

			BString				Evaluate(const char* expressionString);
			int64				EvaluateToInt64(const char* expressionString);
			double				EvaluateToDouble(const char* expressionString);

 private:

			MAPM				_ParseBinary();
			MAPM				_ParseSum();
			MAPM				_ParseProduct();
			MAPM				_ParsePower();
			MAPM				_ParseUnary();
			void				_InitArguments(MAPM values[],
									int32 argumentCount);
			MAPM				_ParseFunction(const Token& token);
			MAPM				_ParseAtom();

			void				_EatToken(int32 type);

			Tokenizer*			fTokenizer;
};

#endif // EXPRESSION_PARSER_H
