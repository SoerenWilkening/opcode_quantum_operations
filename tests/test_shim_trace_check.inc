/* tests/test_shim_trace_check.inc — an INDEPENDENT reader of the trace
 * contract, for tests/test_shim_trace.c.
 *
 * IT SHARES NOTHING WITH THE PRODUCER, and that is the whole reason it is
 * hand-written rather than a call back into `shim/cq_shim_trace.c`. The Step 18
 * lesson applies verbatim: an oracle that reuses one of the implementation's
 * constants is blind to exactly what that constant gets wrong, and it AGREES
 * with the bug rather than failing. So the rules below are transcribed from
 * `qec/docs/HOST_LANGUAGE_HANDOFF.md` §§3-6 — not from cq_shim_trace.c — and
 * nothing here includes a shim header.
 *
 * WHAT IT IS NOT. It is not the viewer's parser, and it must not be mistaken
 * for one: `scripts/verifyview/` is 15 modules and lives in a repository this
 * one neither pins nor can build (the same reason L6 is opt-in). What this
 * checks is the handful of rules §9's table calls a FATAL PARSE ERROR, which is
 * the set a producer can get wrong:
 *
 *   - a `#REGISTER` after the first bracket, a duplicate index inside one
 *     `qubits=` list, or one index in two registers (§3);
 *   - a duplicate register NAME (§3);
 *   - a nested `op begin`, an orphan `op end`, a payloaded `op end`, or an op
 *     still open at EOF (§4);
 *   - a token outside the §5 charset, or a present-but-empty `in=`/`out=`;
 *   - a GATE LINE OUTSIDE ANY BRACKET (§6 rule 1) — the coverage rule, and the
 *     only one of these that a producer breaks by FORGETTING rather than by
 *     mis-spelling.
 *
 * A GATE LINE IS ANY NON-BLANK LINE NOT STARTING WITH `#`, which is exact for
 * this library rather than a heuristic: measured over a full trace, every such
 * line is one of CX / X / Z / H / S / MZ / MX and every `#` line is `#PATCH`,
 * `#REGISTER` or a `# STAGE:` marker. If the library ever grows a non-gate,
 * non-`#` line this reader becomes conservative in the LOUD direction — it
 * would report a gate outside a bracket that is not one — which is the safe
 * way round for a checker. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* `CHECK` prints its condition, which for a counter says only "it was not
 * zero"; a conformance failure has to say WHICH rule and by how much, because
 * the whole file is one call away from every case. `cq_h_fail` already takes a
 * format, so this is the harness's own reporting reached directly. */
#define CHECK_MSG(cond, ...)                                                  \
    do { if (!(cond)) cq_h_fail(__FILE__, __LINE__, __VA_ARGS__); } while (0)

enum { CQ_CONF_IDX_MAX = 512, CQ_CONF_OPS_MAX = 32, CQ_CONF_LABEL = 96 };

typedef struct {
    int  n_reg, n_ops, n_gates;
    int  gate_outside;      /* §6 rule 1 — the coverage failure */
    int  nested, orphan_end, payload_end, open_at_eof;
    int  late_register, dup_index, dup_name, bad_token, empty_list;
    int  missing;           /* the file could not be opened at all */
    char op[CQ_CONF_OPS_MAX][CQ_CONF_LABEL];   /* the op-begin payloads, in order */
    char reg[CQ_CONF_OPS_MAX][CQ_CONF_LABEL];  /* the register NAMES, in order */
    /* AND THE WHOLE LINES, because the name alone cannot express the two things
     * only a multi-lane register has: the `,` separator and the lane ORDER. */
    char reg_line[CQ_CONF_OPS_MAX][CQ_CONF_LABEL];
} cq_conf;

/* §5's authoritative whitelist, transcribed: `[A-Za-z0-9_.$\[\]-]+`. */
static int cq_conf_token(const char *s, size_t n)
{
    size_t i;
    if (n == 0u) return 0;
    for (i = 0u; i < n; i++) {
        const char c = s[i];
        if (isalnum((unsigned char)c)) continue;
        if (c == '_' || c == '.' || c == '$' || c == '[' || c == ']' || c == '-')
            continue;
        return 0;
    }
    return 1;
}

/* One `k=v` pair of an `op begin` payload. `|`-separated elements each have to
 * be a legal token AND non-empty — handoff §4 makes `a||b` and a bare `in=`
 * fatal, which is why an empty element is tracked separately from a bad one. */
static void cq_conf_payload(cq_conf *c, const char *v, size_t n)
{
    size_t start = 0u, i;
    for (i = 0u; i <= n; i++) {
        if (i == n || v[i] == '|') {
            if (i == start)                            c->empty_list++;
            else if (!cq_conf_token(v + start, i - start)) c->bad_token++;
            start = i + 1u;
        }
    }
}

static void cq_conf_op_begin(cq_conf *c, const char *line)
{
    const char *open = strchr(line, '(');
    const char *close = open ? strrchr(open, ')') : NULL;
    const char *p;

    c->n_ops++;
    if (!open || !close || close[1] != '\0') { c->bad_token++; return; }
    if (c->n_ops <= CQ_CONF_OPS_MAX) {
        size_t n = (size_t)(close - open - 1);
        if (n >= CQ_CONF_LABEL) n = CQ_CONF_LABEL - 1u;
        memcpy(c->op[c->n_ops - 1], open + 1, n);
        c->op[c->n_ops - 1][n] = '\0';
    }

    /* Pairs are `k=v`, separated by exactly ", ". Splitting on that literal is
     * itself a check of §4's "no other interior whitespace". */
    p = open + 1;
    while (p < close) {
        const char *comma = strstr(p, ", ");
        const char *end   = (comma && comma < close) ? comma : close;
        const char *eq    = memchr(p, '=', (size_t)(end - p));

        if (!eq) c->bad_token++;
        else     cq_conf_payload(c, eq + 1, (size_t)(end - eq - 1));
        p = (end == close) ? close : end + 2;
    }
}

/* §3, and the two rules that make a register map a PARTITION rather than a
 * list: no index twice inside one `qubits=`, and no index in two registers.
 * Both go through one `seen` array because both are the same claim.
 *
 * THE NAME IS EXTRACTED RATHER THAN COMPARED IN PLACE. A first draft matched
 * `strstr(previous_line, name_ptr)`, which searches for the WHOLE REST of the
 * line — `name=h0 type=i1 qubits=0` — and so could never fire; the duplicate it
 * exists to catch would have gone unreported. Same shape as the "an assertion
 * nobody has seen fail is an assertion nobody has tested" finding, and the
 * reason the case below provokes one. */
static void cq_conf_register(cq_conf *c, char *line, int seen[], int *n_seen,
                             int ops_so_far)
{
    char *q    = strstr(line, "qubits=");
    char *name = strstr(line, "name=");
    char *type = strstr(line, "type=");
    char *tok;
    int   i;

    if (ops_so_far > 0) c->late_register++;
    if (c->n_reg < CQ_CONF_OPS_MAX) {
        /* Taken BEFORE strtok, which writes NULs into `line` below. */
        strncpy(c->reg_line[c->n_reg], line, CQ_CONF_LABEL - 1u);
        c->reg_line[c->n_reg][CQ_CONF_LABEL - 1u] = '\0';
    }
    if (!q || !name || !type) { c->bad_token++; c->n_reg++; return; }

    {   /* the name token, up to the next space or end of line */
        size_t n = strcspn(name + 5, " ");
        if (!cq_conf_token(name + 5, n)) c->bad_token++;
        if (n >= CQ_CONF_LABEL) n = CQ_CONF_LABEL - 1u;
        for (i = 0; i < c->n_reg && i < CQ_CONF_OPS_MAX; i++)
            if (strlen(c->reg[i]) == n && memcmp(c->reg[i], name + 5, n) == 0)
                c->dup_name++;
        if (c->n_reg < CQ_CONF_OPS_MAX) {
            memcpy(c->reg[c->n_reg], name + 5, n);
            c->reg[c->n_reg][n] = '\0';
        }
    }
    {   /* and the type token, on the same §5 charset */
        size_t n = strcspn(type + 5, " ");
        if (!cq_conf_token(type + 5, n)) c->bad_token++;
    }
    c->n_reg++;

    q += strlen("qubits=");
    if (*q == '\0') { c->empty_list++; return; }
    for (tok = strtok(q, ","); tok; tok = strtok(NULL, ",")) {
        int v, k;
        if (tok[strspn(tok, "0123456789")] != '\0' || sscanf(tok, "%d", &v) != 1) {
            c->bad_token++;
            continue;
        }
        for (k = 0; k < *n_seen; k++) if (seen[k] == v) c->dup_index++;
        if (*n_seen < CQ_CONF_IDX_MAX) seen[(*n_seen)++] = v;
    }
}

static void cq_conf_scan(const char *path, cq_conf *c)
{
    FILE *f = fopen(path, "r");
    char  line[4096];
    int   seen[CQ_CONF_IDX_MAX], n_seen = 0, depth = 0;

    memset(c, 0, sizeof *c);
    if (!f) { c->missing = 1; return; }

    while (fgets(line, (int)sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0u) continue;

        if (line[0] != '#') {                       /* a gate line (see above) */
            c->n_gates++;
            if (depth == 0) c->gate_outside++;
            continue;
        }
        if (strncmp(line, "#REGISTER ", 10) == 0) {
            cq_conf_register(c, line, seen, &n_seen, c->n_ops);
            continue;
        }
        if (strcmp(line, "# STAGE: op end") == 0) {
            if (depth == 0) c->orphan_end++; else depth--;
            continue;
        }
        if (strncmp(line, "# STAGE: op end", 15) == 0) { c->payload_end++; continue; }
        if (strncmp(line, "# STAGE: op begin ", 18) == 0) {
            if (depth > 0) c->nested++;
            depth++;
            cq_conf_op_begin(c, line + 18);
        }
    }
    c->open_at_eof = depth;
    fclose(f);
}

/* The one call every case makes: EVERY fatal-parse-error counter is zero. It is
 * spelled as one function rather than as ten CHECKs per case so that a case
 * says what it is FOR — the alias shape, the swap row — and this says what
 * conformance is. */
static void cq_conf_ok(const cq_conf *c, const char *what)
{
    CHECK_MSG(!c->missing,        "%s: the trace file is not there", what);
    CHECK_MSG(!c->gate_outside,   "%s: %d gate line(s) outside any bracket",
              what, c->gate_outside);
    CHECK_MSG(!c->nested,         "%s: %d nested op begin", what, c->nested);
    CHECK_MSG(!c->orphan_end,     "%s: %d orphan op end", what, c->orphan_end);
    CHECK_MSG(!c->payload_end,    "%s: %d payloaded op end", what, c->payload_end);
    CHECK_MSG(!c->open_at_eof,    "%s: %d op open at EOF", what, c->open_at_eof);
    CHECK_MSG(!c->late_register,  "%s: %d #REGISTER after the first bracket",
              what, c->late_register);
    CHECK_MSG(!c->dup_index,      "%s: %d overlapping qubit index", what, c->dup_index);
    CHECK_MSG(!c->dup_name,       "%s: %d duplicate register name", what, c->dup_name);
    CHECK_MSG(!c->bad_token,      "%s: %d malformed token/payload", what, c->bad_token);
    CHECK_MSG(!c->empty_list,     "%s: %d empty in=/out=/qubits= list", what, c->empty_list);
}
