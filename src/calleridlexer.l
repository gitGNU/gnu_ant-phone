%option prefix="callerid_"
%option nounput
%{
/*
 * saved caller id file lexer
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2002, 2003 Roland Stigge
 *
 * ANT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ANT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ANT; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define YYSTYPE char *

#include <string.h>
#include <ctype.h>
#include "globals.h"
#include "calleridparser.h"
YYLTYPE callerid_lloc;

void callerid_locate();
char *strdup_trim(char *text);
%}

LETTER      [a-zA-Z]
INOUT       IN\ *|OUT\ *
DIGIT       [0-9]
YEAR        {DIGIT}{DIGIT}{DIGIT}{DIGIT}
MD          {DIGIT}{DIGIT}
HMS         {DIGIT}{DIGIT}
DATE        {YEAR}-{MD}-{MD}\ {HMS}:{HMS}:{HMS}\ *
NOEM        {LETTER}|{DIGIT}|[-:)(*#\ ]
NUMBER      ({DIGIT}|[*#+?A-D])+\ *
NUMBERORMSG {NOEM}*

%%

"|"|!!                         callerid_locate(); return CID_TOKEN_DELIMITER;
{DATE}                         {
                                 callerid_locate();
                                 callerid_lval = strdup_trim(callerid_text);
                                 return CID_TOKEN_DATE;
                               }
{INOUT}                        {
                                 callerid_locate();
				 callerid_lval = strdup_trim(callerid_text);
				 return CID_TOKEN_TYPE;
			       }
{NUMBERORMSG}                  {
                                 callerid_locate();
                                 callerid_lval = strdup_trim(callerid_text);
				 return CID_TOKEN_TEXT;
			       }

\n                             callerid_locate(); return *callerid_text;
.                              callerid_locate();
                               /* eat up rest (handled as white space) */

%%

/* For portability's sake */
int callerid_wrap() { return 1; }

/*
 * adjusts callerid_lloc according to callerid_text
 */
void callerid_locate() {
  char* temp;

  callerid_lloc.first_line = callerid_lloc.last_line;
  callerid_lloc.first_column = callerid_lloc.last_column;
  
  for (temp = yytext; *temp != '\0'; temp++) {
    if (*temp == '\n') {
      ++ callerid_lloc.last_line;
      callerid_lloc.last_column = 1;
    } else {
      ++ callerid_lloc.last_column;
    }
  }
}

char *strdup_trim(char *text)
{
  char *end, *data;

  /* skip leading spaces */
  while (*text && isspace(*text))
    ++text;

  /* skip trailing spaces */
  end = text + strlen(text);
  while (--end >= text) {
    if (!isspace(*end))
      break;
  }
  ++end;  /* points just behind text */

  /* allocate new string */
  data = (char*) malloc((end - text) + 1);
  if (!data)
    return strdup(text);  /* fallback */
  memcpy(data, text, end - text);
  data[end - text] = 0;
  return data;
}
