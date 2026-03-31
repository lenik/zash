#ifndef ZASH_DRIVER_HPP
#define ZASH_DRIVER_HPP

#ifdef __cplusplus
extern "C" {
#endif

struct ZashProgram;

/** Called from bison yyerror; records message for zash_parse_last_error(). */
void zash_yyerror_record(const char *msg);

/** Last yyerror text from the most recent zash_parse_line (same thread); "" if none. */
const char *zash_parse_last_error(void);

/** Parse one line (append '\\n' internally). Returns 0 on success; *out owned by caller. */
int zash_parse_line(const char *line, struct ZashProgram **out);

#ifdef __cplusplus
}
#endif

#endif
