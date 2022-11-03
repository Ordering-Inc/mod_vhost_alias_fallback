#include <stdio.h>
#include "apr.h"
#include "apr_strings.h"
#include "apr_hash.h"
#include "apr_lib.h"
#include "apr_want.h"
#include "ap_config.h"
#include "ap_provider.h"
#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"

module AP_MODULE_DECLARE_DATA vhost_alias_fallback_module;

typedef enum {
    VHOST_ALIAS_UNSET, VHOST_ALIAS_NONE, VHOST_ALIAS_NAME, VHOST_ALIAS_IP
} mvaf_mode_e;

typedef struct mvaf_sconf_t {
    const char *doc_root;
    const char *doc_root_fallback;
    mvaf_mode_e doc_root_mode;
} mvaf_sconf_t;

static int vhost_alias_set_doc_root_name;

static int dirExists(const char *path)
{
    struct stat info;
    if (stat(path, &info) != 0)
        return 0;
    else if (info.st_mode & S_IFDIR)
        return 1;
    else
        return 0;
}

static const char *vhost_alias_set(cmd_parms *cmd, void *dummy, const char *map, const char *fallback)
{
    mvaf_sconf_t *conf;
    mvaf_mode_e mode, *pmode;
    const char **pmap;
    const char **ifallback;
    const char *p;

    conf = (mvaf_sconf_t *) ap_get_module_config(cmd->server->module_config,
                                                &vhost_alias_fallback_module);

    ifallback = &conf->doc_root_fallback;
    *ifallback = fallback;
    /* there ought to be a better way of doing this */
    if (&vhost_alias_set_doc_root_name == cmd->info) {
        mode = VHOST_ALIAS_NAME;
        pmap = &conf->doc_root;
        pmode = &conf->doc_root_mode;
    }
    else {
        return "INTERNAL ERROR: unknown command info";
    }

    if (!ap_os_is_path_absolute(cmd->pool, map)) {
        if (ap_cstr_casecmp(map, "none")) {
            return "format string must be an absolute path, or 'none'";
        }
        *pmap = NULL;
        *pmode = VHOST_ALIAS_NONE;
        return NULL;
    }

    /* sanity check */
    p = map;
    while (*p != '\0') {
        if (*p++ != '%') {
            continue;
        }
        /* we just found a '%' */
        if (*p == 'p' || *p == '%') {
            ++p;
            continue;
        }
        /* optional dash */
        if (*p == '-') {
            ++p;
        }
        /* digit N */
        if (apr_isdigit(*p)) {
            ++p;
        }
        else {
            return "syntax error in format string";
        }
        /* optional plus */
        if (*p == '+') {
            ++p;
        }
        /* do we end here? */
        if (*p != '.') {
            continue;
        }
        ++p;
        /* optional dash */
        if (*p == '-') {
            ++p;
        }
        /* digit M */
        if (apr_isdigit(*p)) {
            ++p;
        }
        else {
            return "syntax error in format string";
        }
        /* optional plus */
        if (*p == '+') {
            ++p;
        }
    }
    *pmap = map;
    *pmode = mode;
    return NULL;
}

static APR_INLINE void vhost_alias_checkspace(request_rec *r, char *buf,
                                             char **pdest, int size)
{
    /* XXX: what if size > HUGE_STRING_LEN? */
    if (*pdest + size > buf + HUGE_STRING_LEN) {
        **pdest = '\0';
        if (r->filename) {
            r->filename = apr_pstrcat(r->pool, r->filename, buf, NULL);
        }
        else {
            r->filename = apr_pstrdup(r->pool, buf);
        }
        *pdest = buf;
    }
}

static void vhost_alias_interpolate(request_rec *r, const char *name,
                                    const char *map, const char *fallback, const char *uri)
{
    /* 0..9 9..0 */
    enum { MAXDOTS = 19 };
    const char *dots[MAXDOTS+1];
    int ndots;

    char buf[HUGE_STRING_LEN];
    char *dest;
    const char *docroot;

    int N, M, Np, Mp, Nd, Md;
    const char *start, *end;

    const char *p;

    ndots = 0;
    dots[ndots++] = name-1; /* slightly naughty */
    for (p = name; *p; ++p) {
        if (*p == '.' && ndots < MAXDOTS) {
            dots[ndots++] = p;
        }
    }
    dots[ndots] = p;

    r->filename = NULL;

    dest = buf;
    while (*map) {
        if (*map != '%') {
            /* normal characters */
            vhost_alias_checkspace(r, buf, &dest, 1);
            *dest++ = *map++;
            continue;
        }
        /* we are in a format specifier */
        ++map;
        /* %% -> % */
        if (*map == '%') {
            ++map;
            vhost_alias_checkspace(r, buf, &dest, 1);
            *dest++ = '%';
            continue;
        }
        /* port number */
        if (*map == 'p') {
            ++map;
            /* no. of decimal digits in a short plus one */
            vhost_alias_checkspace(r, buf, &dest, 7);
            dest += apr_snprintf(dest, 7, "%d", ap_get_server_port(r));
            continue;
        }
        /* deal with %-N+.-M+ -- syntax is already checked */
        M = 0;   /* value */
        Np = Mp = 0; /* is there a plus? */
        Nd = Md = 0; /* is there a dash? */
        if (*map == '-') ++map, Nd = 1;
        N = *map++ - '0';
        if (*map == '+') ++map, Np = 1;
        if (*map == '.') {
            ++map;
            if (*map == '-') {
                ++map, Md = 1;
            }
            M = *map++ - '0';
            if (*map == '+') {
                ++map, Mp = 1;
            }
        }
        /* note that N and M are one-based indices, not zero-based */
        start = dots[0]+1; /* ptr to the first character */
        end = dots[ndots]; /* ptr to the character after the last one */
        if (N != 0) {
            if (N > ndots) {
                start = "_";
                end = start+1;
            }
            else if (!Nd) {
                start = dots[N-1]+1;
                if (!Np) {
                    end = dots[N];
                }
            }
            else {
                if (!Np) {
                    start = dots[ndots-N]+1;
                }
                end = dots[ndots-N+1];
            }
        }
        if (M != 0) {
            if (M > end - start) {
                start = "_";
                end = start+1;
            }
            else if (!Md) {
                start = start+M-1;
                if (!Mp) {
                    end = start+1;
                }
            }
            else {
                if (!Mp) {
                    start = end-M;
                }
                end = end-M+1;
            }
        }
        vhost_alias_checkspace(r, buf, &dest, end - start);
        for (p = start; p < end; ++p) {
            *dest++ = apr_tolower(*p);
        }
    }
    /* no double slashes */
    if (dest - buf > 0 && dest[-1] == '/') {
        --dest;
    }
    *dest = '\0';

    if (r->filename)
        docroot = apr_pstrcat(r->pool, r->filename, buf, NULL);
    else
        docroot = apr_pstrdup(r->pool, buf);

    if (dirExists(docroot) == 1) {
        r->filename = apr_pstrcat(r->pool, docroot, uri, NULL);
        ap_set_context_info(r, NULL, docroot);
        ap_set_document_root(r, docroot);
    } else {
        r->filename = apr_pstrcat(r->pool, fallback, uri, NULL);
        ap_set_context_info(r, NULL, fallback);
        ap_set_document_root(r, fallback);
    }
}

static int mvaf_translate(request_rec *r)
{
    mvaf_sconf_t *conf;
    const char *name, *map, *uri;
    mvaf_mode_e mode;

    conf = (mvaf_sconf_t *) ap_get_module_config(r->server->module_config,
                                                &vhost_alias_fallback_module);
    if (r->uri[0] == '/') {
        mode = conf->doc_root_mode;
        map = conf->doc_root;
        uri = r->uri;
    }
    else {
        return DECLINED;
    }

    if (mode == VHOST_ALIAS_NAME) {
        name = ap_get_server_name(r);
    }
    else if (mode == VHOST_ALIAS_IP) {
        name = r->connection->local_ip;
    }
    else {
        return DECLINED;
    }

    /* ### There is an optimization available here to determine the
     * absolute portion of the path from the server config phase,
     * through the first % segment, and note that portion of the path
     * canonical_path buffer.
     */
    r->canonical_filename = "";
    vhost_alias_interpolate(r, name, map, conf->doc_root_fallback, uri);

    return OK;
}

static void *mvaf_merge_server_config(apr_pool_t *p, void *parentv, void *childv)
{
    mvaf_sconf_t *parent = (mvaf_sconf_t *) parentv;
    mvaf_sconf_t *child = (mvaf_sconf_t *) childv;
    mvaf_sconf_t *conf;

    conf = (mvaf_sconf_t *) apr_pcalloc(p, sizeof(*conf));
    if (child->doc_root_mode == VHOST_ALIAS_UNSET) {
        conf->doc_root_mode = parent->doc_root_mode;
        conf->doc_root = parent->doc_root;
        conf->doc_root_fallback = parent->doc_root_fallback;
    }
    else {
        conf->doc_root_mode = child->doc_root_mode;
        conf->doc_root = child->doc_root;
        conf->doc_root_fallback = child->doc_root_fallback;
    }
    return conf;
}

static void *mvaf_create_server_config(apr_pool_t *p, server_rec *s)
{
    mvaf_sconf_t *conf;

    conf = (mvaf_sconf_t *) apr_pcalloc(p, sizeof(mvaf_sconf_t));
    conf->doc_root = NULL;
    conf->doc_root_fallback = NULL;
    conf->doc_root_mode = VHOST_ALIAS_UNSET;
    return conf;
}

static const command_rec mvaf_commands[] =
{
    AP_INIT_TAKE2("VirtualDocumentRootWithFallback", vhost_alias_set,
                &vhost_alias_set_doc_root_name, RSRC_CONF,
                "how to create the DocumentRoot based on the host with fallback"),
    { NULL }
};

static void register_hooks(apr_pool_t *p)
{
    static const char * const aszPre[]={ "mod_alias.c","mod_userdir.c",NULL };

    ap_hook_translate_name(mvaf_translate, aszPre, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA vhost_alias_fallback_module =
{
    STANDARD20_MODULE_STUFF,
    NULL,                           /* Per-directory configuration handler */
    NULL,                           /* Merge handler for per-directory configurations */
    mvaf_create_server_config,      /* Per-server configuration handler */
    mvaf_merge_server_config,       /* Merge handler for per-server configurations */
    mvaf_commands,                  /* Any directives we may have for httpd */
    register_hooks                  /* Our hook registering function */
};
