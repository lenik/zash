%code requires {
#include "zash_ast.h"
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zash_ast.h"

void yyerror(const char *msg);
void zash_yyerror_record(const char *msg);
int yylex(void);

ZashProgram *zash_parse_result;

static ZashCmd *merge_cmd(ZashCmd *a, ZashCmd *b) {
    if (!a)
        return b;
    if (!b)
        return a;
    int i;
    for (i = 0; i < b->nassigns; i++)
        zash_cmd_add_assign(a, b->assigns[i]);
    for (i = 0; i < b->argc; i++)
        zash_cmd_add_word(a, b->argv[i]);
    for (i = 0; i < b->nredirs; i++)
        zash_cmd_add_redir(a, b->redirs[i]);
    free(b->assigns);
    free(b->argv);
    free(b->redirs);
    free(b);
    return a;
}

%}

%define parse.error verbose

%union {
    char *str;
    ZashCmd *cmd;
    ZashPipeline *pl;
    ZashList *list;
    ZashStmt *stmt;
    ZashProgram *prog;
    ZashCaseArm *carm;
    struct {
        char **w;
        int n;
    } wl;
}

%token <str> WORD ASSIGN
%token IF THEN ELSE FI FOR DO DONE IN WHILE FUNCTION CASE ESAC
%token SWITCH
%token PIPE SEMI NEWLINE GREAT DGREAT LESS ANDAND OROR
%token LPAREN RPAREN LBRACE RBRACE DSEMI

%type <cmd> cmd_prefix cmd_suffix simple_command redirection
%type <pl> pipeline
%type <list> compound_list
%type <stmt> stmt if_stmt for_stmt while_stmt func_stmt case_stmt
%type <prog> program stmt_list brace_stmt_list compound_body_list
%type <carm> case_arms case_arm
%type <wl> word_list pattern_list

%start program

%%

program
    : stmt_list { zash_parse_result = $1; }
    | /* empty */ {
            zash_parse_result = zash_program_new();
        }
    ;

stmt_list
    : stmt_list separator stmt {
            if ($3)
                zash_program_append($1, $3);
            $$ = $1;
        }
    | stmt_list separator { $$ = $1; }
    | stmt {
            $$ = zash_program_new();
            if ($1)
                zash_program_append($$, $1);
        }
    ;

separator
    : NEWLINE
    | SEMI
    ;

stmt
    : compound_list { $$ = zash_stmt_list($1); }
    | if_stmt
    | for_stmt
    | while_stmt
    | func_stmt
    | case_stmt
    ;

pipeline
    : simple_command { $$ = zash_pipeline_append(NULL, $1); }
    | pipeline PIPE simple_command { $$ = zash_pipeline_append($1, $3); }
    ;

simple_command
    : cmd_prefix cmd_suffix { $$ = merge_cmd($1, $2); }
    | cmd_prefix { $$ = $1; }
    | cmd_suffix { $$ = $1; }
    ;

cmd_prefix
    : ASSIGN {
            ZashCmd *c = zash_cmd_new();
            zash_cmd_add_assign(c, $1);
            $$ = c;
        }
    | cmd_prefix ASSIGN {
            zash_cmd_add_assign($1, $2);
            $$ = $1;
        }
    | redirection { $$ = $1; }
    | cmd_prefix redirection { $$ = merge_cmd($1, $2); }
    ;

cmd_suffix
    : WORD {
            ZashCmd *c = zash_cmd_new();
            zash_cmd_add_word(c, $1);
            $$ = c;
        }
    | cmd_suffix WORD {
            zash_cmd_add_word($1, $2);
            $$ = $1;
        }
    /* name=value after a command word (e.g. alias ls='ls -al', export FOO='x y') */
    | cmd_suffix ASSIGN {
            zash_cmd_add_word($1, $2);
            $$ = $1;
        }
    | redirection { $$ = $1; }
    | cmd_suffix redirection { $$ = merge_cmd($1, $2); }
    ;

redirection
    : GREAT WORD {
            ZashCmd *c = zash_cmd_new();
            zash_cmd_add_redir(c, zash_redir_new(0, $2));
            $$ = c;
        }
    | DGREAT WORD {
            ZashCmd *c = zash_cmd_new();
            zash_cmd_add_redir(c, zash_redir_new(1, $2));
            $$ = c;
        }
    | LESS WORD {
            ZashCmd *c = zash_cmd_new();
            zash_cmd_add_redir(c, zash_redir_new(2, $2));
            $$ = c;
        }
    ;

compound_list
    : pipeline { $$ = zash_list_append(NULL, $1); }
    | compound_list separator pipeline { $$ = zash_list_append_sep($1, ZASH_SEP_SEMI, $3); }
    | compound_list separator { $$ = $1; }
    | compound_list ANDAND pipeline { $$ = zash_list_append_sep($1, ZASH_SEP_AND, $3); }
    | compound_list OROR pipeline { $$ = zash_list_append_sep($1, ZASH_SEP_OR, $3); }
    ;

if_stmt
    : IF compound_list THEN compound_list FI { $$ = zash_stmt_if($2, $4, NULL); }
    | IF compound_list THEN compound_list ELSE compound_list FI {
            $$ = zash_stmt_if($2, $4, $6);
        }
    ;

word_list
    : WORD {
            $$.w = (char **)malloc(sizeof(char *));
            $$.w[0] = $1;
            $$.n = 1;
        }
    | word_list WORD {
            $$ = $1;
            $$.w = (char **)realloc($$.w, (size_t)($$.n + 1) * sizeof(char *));
            $$.w[$$.n++] = $2;
        }
    ;

for_stmt
    : FOR WORD IN word_list DO compound_list DONE {
            $$ = zash_stmt_for($2, $4.w, $4.n, $6);
        }
    | FOR WORD IN word_list separator DO compound_list DONE {
            $$ = zash_stmt_for($2, $4.w, $4.n, $7);
        }
    ;

while_stmt
    : WHILE compound_list DO compound_list DONE { $$ = zash_stmt_while($2, $4); }
    | WHILE compound_list separator DO compound_list DONE { $$ = zash_stmt_while($2, $5); }
    ;

brace_stmt_list
    : /* empty */ { $$ = zash_program_new(); }
    | brace_stmt_list separator stmt {
            if ($3)
                zash_program_append($1, $3);
            $$ = $1;
        }
    | brace_stmt_list separator { $$ = $1; }
    | stmt {
            $$ = zash_program_new();
            if ($1)
                zash_program_append($$, $1);
        }
    ;

compound_body_list
    : LBRACE brace_stmt_list RBRACE { $$ = $2; }
    ;

func_stmt
    : FUNCTION WORD LPAREN RPAREN compound_body_list { $$ = zash_stmt_func($2, $5); }
    | WORD LPAREN RPAREN compound_body_list { $$ = zash_stmt_func($1, $4); }
    ;

case_stmt
    : CASE WORD linebreak IN linebreak case_arms ESAC { $$ = zash_stmt_case($2, $6); }
    | SWITCH WORD linebreak IN linebreak case_arms ESAC { $$ = zash_stmt_case($2, $6); }
    ;

linebreak
    : /* empty */
    | NEWLINE
    ;

case_arms
    : case_arm { $$ = $1; }
    | case_arms case_arm {
            ZashCaseArm *p = $1;
            while (p->next)
                p = p->next;
            p->next = $2;
            $$ = $1;
        }
    ;

case_arm
    : pattern_list RPAREN compound_list DSEMI {
            $$ = zash_case_arm_new($1.w, $1.n, $3);
        }
    ;

pattern_list
    : WORD {
            $$.w = (char **)malloc(sizeof(char *));
            $$.w[0] = $1;
            $$.n = 1;
        }
    | pattern_list PIPE WORD {
            $$ = $1;
            $$.w = (char **)realloc($$.w, (size_t)($$.n + 1) * sizeof(char *));
            $$.w[$$.n++] = $3;
        }
    ;

%%

void yyerror(const char *msg) {
    zash_yyerror_record(msg);
}
