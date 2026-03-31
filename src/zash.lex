%{
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "zash_ast.h"
#include "zash.tab.h"

static char *zash_dup(const char *s) {
    return s ? strdup(s) : NULL;
}

/* Decode flex yytext of a double-quoted string (includes wrapping "). */
static char *decode_double_quoted(const char *yy, size_t yylen) {
    if (yylen < 2 || yy[0] != '"' || yy[yylen - 1] != '"')
        return zash_strdup("");
    const char *p = yy + 1;
    const char *end = yy + yylen - 1;
    size_t cap = yylen + 1;
    char *out = (char *)malloc(cap);
    if (!out)
        return NULL;
    char *q = out;
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            ++p;
            *q++ = *p++;
        } else
            *q++ = *p++;
    }
    *q = '\0';
    return out;
}

/* Build "NAME=decoded" from yytext matching NAME="..." */
static char *assign_double_quoted(const char *yy) {
    const char *eq = strchr(yy, '=');
    if (!eq || eq[1] != '"')
        return zash_dup(yy);
    size_t keylen = (size_t)(eq - yy + 1);
    char *inner = decode_double_quoted(eq + 1, strlen(eq + 1));
    if (!inner)
        return NULL;
    size_t ilen = strlen(inner);
    char *r = (char *)malloc(keylen + ilen + 1);
    if (!r) {
        free(inner);
        return NULL;
    }
    memcpy(r, yy, keylen);
    memcpy(r + keylen, inner, ilen + 1);
    free(inner);
    return r;
}

/* Build "NAME=decoded" from yytext matching NAME='...' */
static char *assign_single_quoted(const char *yy) {
    const char *eq = strchr(yy, '=');
    if (!eq || eq[1] != '\'')
        return zash_dup(yy);
    size_t keylen = (size_t)(eq - yy + 1);
    const char *a = eq + 2;
    const char *b = strrchr(a, '\'');
    if (!b)
        return zash_dup(yy);
    size_t vlen = (size_t)(b - a);
    char *r = (char *)malloc(keylen + vlen + 1);
    if (!r)
        return NULL;
    memcpy(r, yy, keylen);
    memcpy(r + keylen, a, vlen);
    r[keylen + vlen] = '\0';
    return r;
}

static int kw(const char *t) {
    if (!strcmp(t, "if")) return IF;
    if (!strcmp(t, "then")) return THEN;
    if (!strcmp(t, "else")) return ELSE;
    if (!strcmp(t, "fi")) return FI;
    if (!strcmp(t, "for")) return FOR;
    if (!strcmp(t, "do")) return DO;
    if (!strcmp(t, "done")) return DONE;
    if (!strcmp(t, "in")) return IN;
    if (!strcmp(t, "while")) return WHILE;
    if (!strcmp(t, "function")) return FUNCTION;
    if (!strcmp(t, "case")) return CASE;
    if (!strcmp(t, "esac")) return ESAC;
    if (!strcmp(t, "switch")) return SWITCH;
    return 0;
}
%}

%option noyywrap nounput noinput yylineno

%%

[ \t\r]+    ;

\n          return NEWLINE;

";;"        return DSEMI;

">>"        return DGREAT;
">"         return GREAT;
"<"         return LESS;

"||"        return OROR;
"&&"        return ANDAND;

"|"         return PIPE;
";"         return SEMI;

"("         return LPAREN;
")"         return RPAREN;
"{"         return LBRACE;
"}"         return RBRACE;

[a-zA-Z_][a-zA-Z0-9_]*=\"([^\\\"]|\\.)*\" {
            yylval.str = assign_double_quoted(yytext);
            if (!yylval.str)
                yylval.str = zash_dup(yytext);
            return ASSIGN;
        }

[a-zA-Z_][a-zA-Z0-9_]*=\'[^\']*\' {
            yylval.str = assign_single_quoted(yytext);
            if (!yylval.str)
                yylval.str = zash_dup(yytext);
            return ASSIGN;
        }

[a-zA-Z_][a-zA-Z0-9_]*=([^ \t\n|;<>{}()'\"]|\\.)+ {
            yylval.str = zash_dup(yytext);
            return ASSIGN;
        }

[a-zA-Z_][a-zA-Z0-9_]*= {
            yylval.str = zash_dup(yytext);
            return ASSIGN;
        }

\"([^\\\"]|\\.)*\" {
            yylval.str = decode_double_quoted(yytext, strlen(yytext));
            if (!yylval.str)
                yylval.str = zash_strdup("");
            return WORD;
        }

\'[^\']*\' {
            size_t n = strlen(yytext);
            if (n < 2) {
                yylval.str = zash_dup("");
                return WORD;
            }
            char *out = malloc(n);
            if (!out)
                return WORD;
            memcpy(out, yytext + 1, n - 2);
            out[n - 2] = 0;
            yylval.str = out;
            return WORD;
        }

[a-zA-Z_][a-zA-Z0-9_]* {
            int t = kw(yytext);
            if (t)
                return t;
            yylval.str = zash_dup(yytext);
            return WORD;
        }

[^ \t\n|;<>{}()\"\'&]+ {
            yylval.str = zash_dup(yytext);
            return WORD;
        }

.           return yytext[0];

%%
