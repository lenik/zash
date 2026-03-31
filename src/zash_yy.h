#ifndef ZASH_YY_H
#define ZASH_YY_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
int yylex(void);
int yyparse(void);

#ifdef __cplusplus
}
#endif

#endif
