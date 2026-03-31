#include "zash_driver.hpp"

#include "zash.tab.h"
#include "zash_yy.h"

#include "zash_ast.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ZashProgram *zash_parse_result;

static pthread_mutex_t zash_parse_mutex = PTHREAD_MUTEX_INITIALIZER;

static char zash_errbuf[512];

void zash_yyerror_record(const char *msg) {
    if (!msg || !msg[0])
        msg = "syntax error";
    snprintf(zash_errbuf, sizeof zash_errbuf, "%s", msg);
}

const char *zash_parse_last_error(void) {
    return zash_errbuf;
}

int zash_parse_line(const char *line, ZashProgram **out) {
    if (!out)
        return -1;
    *out = NULL;
    if (!line)
        return 0;

    size_t n = strlen(line);
    char *buf = (char *)malloc(n + 2);
    if (!buf)
        return -1;
    memcpy(buf, line, n);
    buf[n] = '\n';
    buf[n + 1] = '\0';

    pthread_mutex_lock(&zash_parse_mutex);
    zash_errbuf[0] = '\0';
    zash_parse_result = NULL;
    YY_BUFFER_STATE bs = yy_scan_string(buf);
    free(buf);
    int r = yyparse();
    yy_delete_buffer(bs);
    pthread_mutex_unlock(&zash_parse_mutex);
    *out = zash_parse_result;
    zash_parse_result = NULL;
    return r;
}
