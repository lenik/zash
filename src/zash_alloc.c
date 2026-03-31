#include "zash_ast.h"

#include <stdlib.h>
#include <string.h>

char *zash_strdup(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

ZashRedir *zash_redir_new(int op, char *path) {
    ZashRedir *r = (ZashRedir *)calloc(1, sizeof(ZashRedir));
    if (!r) {
        free(path);
        return NULL;
    }
    r->op = op;
    r->path = path;
    return r;
}

ZashCmd *zash_cmd_new(void) {
    return (ZashCmd *)calloc(1, sizeof(ZashCmd));
}

void zash_cmd_add_assign(ZashCmd *c, char *kv) {
    if (!c || !kv)
        return;
    char **na = (char **)realloc(c->assigns, (size_t)(c->nassigns + 1) * sizeof(char *));
    if (!na) {
        free(kv);
        return;
    }
    c->assigns = na;
    c->assigns[c->nassigns++] = kv;
}

void zash_cmd_add_word(ZashCmd *c, char *w) {
    if (!c || !w)
        return;
    char **na = (char **)realloc(c->argv, (size_t)(c->argc + 1) * sizeof(char *));
    if (!na) {
        free(w);
        return;
    }
    c->argv = na;
    c->argv[c->argc++] = w;
}

void zash_cmd_add_redir(ZashCmd *c, ZashRedir *r) {
    if (!c || !r)
        return;
    ZashRedir **na =
        (ZashRedir **)realloc(c->redirs, (size_t)(c->nredirs + 1) * sizeof(ZashRedir *));
    if (!na) {
        free(r->path);
        free(r);
        return;
    }
    c->redirs = na;
    c->redirs[c->nredirs++] = r;
}

ZashPipeline *zash_pipeline_append(ZashPipeline *pl, ZashCmd *cmd) {
    if (!cmd)
        return pl;
    if (!pl) {
        pl = (ZashPipeline *)calloc(1, sizeof(ZashPipeline));
        if (!pl)
            return NULL;
    }
    ZashCmd **nc = (ZashCmd **)realloc(pl->cmds, (size_t)(pl->ncmds + 1) * sizeof(ZashCmd *));
    if (!nc)
        return pl;
    pl->cmds = nc;
    pl->cmds[pl->ncmds++] = cmd;
    return pl;
}

ZashList *zash_list_append(ZashList *list, ZashPipeline *pl) {
    if (!pl)
        return list;
    if (!list) {
        list = (ZashList *)calloc(1, sizeof(ZashList));
        if (!list)
            return NULL;
    }
    ZashPipeline **np = (ZashPipeline **)realloc(list->pipes,
                                                 (size_t)(list->npipes + 1) * sizeof(ZashPipeline *));
    if (!np)
        return list;
    list->pipes = np;
    list->pipes[list->npipes++] = pl;
    return list;
}

ZashList *zash_list_append_sep(ZashList *list, int sep, ZashPipeline *pl) {
    if (!pl)
        return list;
    if (!list)
        return zash_list_append(NULL, pl);
    int n = list->npipes;
    if (n < 1)
        return zash_list_append(list, pl);
    int *newops = (int *)realloc(list->ops, (size_t)n * sizeof(int));
    if (!newops)
        return list;
    list->ops = newops;
    list->ops[n - 1] = sep;
    return zash_list_append(list, pl);
}

ZashCaseArm *zash_case_arm_new(char **patterns, int npatterns, ZashList *body) {
    ZashCaseArm *a = (ZashCaseArm *)calloc(1, sizeof(ZashCaseArm));
    if (!a)
        return NULL;
    a->patterns = patterns;
    a->npatterns = npatterns;
    a->body = body;
    return a;
}

void zash_case_arm_chain(ZashCaseArm **head, ZashCaseArm **tail, ZashCaseArm *arm) {
    if (!arm)
        return;
    if (!*head) {
        *head = *tail = arm;
        return;
    }
    (*tail)->next = arm;
    *tail = arm;
}

static void free_cmd(ZashCmd *c) {
    if (!c)
        return;
    for (int i = 0; i < c->nassigns; i++)
        free(c->assigns[i]);
    free(c->assigns);
    for (int i = 0; i < c->argc; i++)
        free(c->argv[i]);
    free(c->argv);
    for (int i = 0; i < c->nredirs; i++) {
        free(c->redirs[i]->path);
        free(c->redirs[i]);
    }
    free(c->redirs);
    free(c);
}

static void free_pipeline(ZashPipeline *pl) {
    if (!pl)
        return;
    for (int i = 0; i < pl->ncmds; i++)
        free_cmd(pl->cmds[i]);
    free(pl->cmds);
    free(pl);
}

static void free_list(ZashList *list);

static void free_case_arms(ZashCaseArm *a) {
    while (a) {
        ZashCaseArm *n = a->next;
        for (int i = 0; i < a->npatterns; i++)
            free(a->patterns[i]);
        free(a->patterns);
        free_list(a->body);
        free(a);
        a = n;
    }
}

static void free_list(ZashList *list) {
    if (!list)
        return;
    for (int i = 0; i < list->npipes; i++)
        free_pipeline(list->pipes[i]);
    free(list->pipes);
    free(list->ops);
    free(list);
}

static void free_stmt(ZashStmt *st) {
    if (!st)
        return;
    switch (st->kind) {
    case ZST_LIST:
        free_list(st->u.line);
        break;
    case ZST_IF:
        free_list(st->u.iff.cond);
        free_list(st->u.iff.then_part);
        free_list(st->u.iff.else_part);
        break;
    case ZST_FOR:
        free(st->u.fr.var);
        for (int i = 0; i < st->u.fr.nitems; i++)
            free(st->u.fr.items[i]);
        free(st->u.fr.items);
        free_list(st->u.fr.body);
        break;
    case ZST_WHILE:
        free_list(st->u.wh.cond);
        free_list(st->u.wh.body);
        break;
    case ZST_FUNC:
        free(st->u.fn.name);
        zash_free_program(st->u.fn.body);
        break;
    case ZST_CASE:
        free(st->u.cs.word);
        free_case_arms(st->u.cs.arms);
        break;
    default:
        break;
    }
    free(st);
}

ZashStmt *zash_stmt_list(ZashList *list) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free_list(list);
        return NULL;
    }
    st->kind = ZST_LIST;
    st->u.line = list;
    return st;
}

ZashStmt *zash_stmt_pipeline(ZashPipeline *pl) {
    return zash_stmt_list(zash_list_append(NULL, pl));
}

ZashStmt *zash_stmt_if(ZashList *cond, ZashList *then_part, ZashList *else_part) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free_list(cond);
        free_list(then_part);
        free_list(else_part);
        return NULL;
    }
    st->kind = ZST_IF;
    st->u.iff.cond = cond;
    st->u.iff.then_part = then_part;
    st->u.iff.else_part = else_part;
    return st;
}

ZashStmt *zash_stmt_for(char *var, char **items, int nitems, ZashList *body) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free(var);
        for (int i = 0; i < nitems; i++)
            free(items[i]);
        free(items);
        free_list(body);
        return NULL;
    }
    st->kind = ZST_FOR;
    st->u.fr.var = var;
    st->u.fr.items = items;
    st->u.fr.nitems = nitems;
    st->u.fr.body = body;
    return st;
}

ZashStmt *zash_stmt_while(ZashList *cond, ZashList *body) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free_list(cond);
        free_list(body);
        return NULL;
    }
    st->kind = ZST_WHILE;
    st->u.wh.cond = cond;
    st->u.wh.body = body;
    return st;
}

ZashStmt *zash_stmt_func(char *name, ZashProgram *body) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free(name);
        zash_free_program(body);
        return NULL;
    }
    st->kind = ZST_FUNC;
    st->u.fn.name = name;
    st->u.fn.body = body;
    return st;
}

ZashStmt *zash_stmt_case(char *word, ZashCaseArm *arms) {
    ZashStmt *st = (ZashStmt *)calloc(1, sizeof(ZashStmt));
    if (!st) {
        free(word);
        free_case_arms(arms);
        return NULL;
    }
    st->kind = ZST_CASE;
    st->u.cs.word = word;
    st->u.cs.arms = arms;
    return st;
}

ZashProgram *zash_program_new(void) {
    return (ZashProgram *)calloc(1, sizeof(ZashProgram));
}

void zash_program_append(ZashProgram *p, ZashStmt *st) {
    if (!p || !st)
        return;
    if (!p->first)
        p->first = p->last = st;
    else {
        p->last->next = st;
        p->last = st;
    }
}

void zash_free_program(ZashProgram *p) {
    if (!p)
        return;
    ZashStmt *st = p->first;
    while (st) {
        ZashStmt *n = st->next;
        free_stmt(st);
        st = n;
    }
    free(p);
}
