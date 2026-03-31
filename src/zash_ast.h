/* C AST for zash(1) — consumed by bison (%union) and interpreter. */
#ifndef ZASH_AST_H
#define ZASH_AST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ZashRedir ZashRedir;
typedef struct ZashCmd ZashCmd;
typedef struct ZashPipeline ZashPipeline;
typedef struct ZashList ZashList;
typedef struct ZashCaseArm ZashCaseArm;
typedef struct ZashStmt ZashStmt;
typedef struct ZashProgram ZashProgram;

struct ZashRedir {
    int op; /* 0 '>', 1 '>>', 2 '<' */
    char *path;
};

struct ZashCmd {
    char **assigns;
    int nassigns;
    char **argv;
    int argc;
    ZashRedir **redirs;
    int nredirs;
};

struct ZashPipeline {
    ZashCmd **cmds;
    int ncmds;
};

/** Separators before pipes[1..]: ZASH_SEP_SEMI (; newline), ZASH_SEP_AND (&&), ZASH_SEP_OR (||). */
#define ZASH_SEP_SEMI 0
#define ZASH_SEP_AND 1
#define ZASH_SEP_OR 2

struct ZashList {
    ZashPipeline **pipes;
    int npipes;
    int *ops; /* length npipes - 1; ops[i] is op before pipes[i + 1] */
};

struct ZashCaseArm {
    char **patterns;
    int npatterns;
    ZashList *body;
    ZashCaseArm *next;
};

typedef enum {
    ZST_LIST = 1,
    ZST_IF,
    ZST_FOR,
    ZST_WHILE,
    ZST_FUNC,
    ZST_CASE,
} ZashStmtKind;

struct ZashStmt {
    int kind;
    union {
        ZashList *line;
        struct {
            ZashList *cond;
            ZashList *then_part;
            ZashList *else_part;
        } iff;
        struct {
            char *var;
            char **items;
            int nitems;
            ZashList *body;
        } fr;
        struct {
            ZashList *cond;
            ZashList *body;
        } wh;
        struct {
            char *name;
            ZashProgram *body;
        } fn;
        struct {
            char *word;
            ZashCaseArm *arms;
        } cs;
    } u;
    ZashStmt *next;
};

struct ZashProgram {
    ZashStmt *first;
    ZashStmt *last;
};

/* Alloc helpers (zash_alloc.c) */
char *zash_strdup(const char *s);
ZashRedir *zash_redir_new(int op, char *path);
ZashCmd *zash_cmd_new(void);
void zash_cmd_add_assign(ZashCmd *c, char *kv);
void zash_cmd_add_word(ZashCmd *c, char *w);
void zash_cmd_add_redir(ZashCmd *c, ZashRedir *r);
ZashPipeline *zash_pipeline_append(ZashPipeline *pl, ZashCmd *cmd);
ZashList *zash_list_append(ZashList *list, ZashPipeline *pl);
ZashList *zash_list_append_sep(ZashList *list, int sep, ZashPipeline *pl);
ZashCaseArm *zash_case_arm_new(char **patterns, int npatterns, ZashList *body);
void zash_case_arm_chain(ZashCaseArm **head, ZashCaseArm **tail, ZashCaseArm *arm);
ZashStmt *zash_stmt_list(ZashList *list);
ZashStmt *zash_stmt_pipeline(ZashPipeline *pl);
ZashStmt *zash_stmt_if(ZashList *cond, ZashList *then_part, ZashList *else_part);
ZashStmt *zash_stmt_for(char *var, char **items, int nitems, ZashList *body);
ZashStmt *zash_stmt_while(ZashList *cond, ZashList *body);
ZashStmt *zash_stmt_func(char *name, ZashProgram *body);
ZashStmt *zash_stmt_case(char *word, ZashCaseArm *arms);
void zash_program_append(ZashProgram *p, ZashStmt *st);
ZashProgram *zash_program_new(void);
void zash_free_program(ZashProgram *p);

#ifdef __cplusplus
}
#endif

#endif
