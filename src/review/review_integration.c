#include "nevercode.h"

char *build_review_prompt(const char *target, const char *focus) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "You are performing a structured code review. "
                           "Analyze the code for issues across these categories:\n\n"
                           "1. **Security** — vulnerabilities, injection risks, auth issues\n"
                           "2. **Performance** — inefficiency, memory leaks, O(n²) patterns\n"
                           "3. **Correctness** — logic errors, edge cases, race conditions\n"
                           "4. **Maintainability** — code clarity, naming, duplication\n"
                           "5. **Best Practices** — error handling, testing, documentation\n\n"
                           "For each finding, output a structured block:\n\n"
                           "```\n"
                           "### [SEVERITY] Title\n"
                           "**File:** path/to/file.ext (lines X-Y)\n"
                           "**Description:** What the issue is and why it matters.\n"
                           "**Suggestion:** How to fix it.\n"
                           "```\n\n"
                           "Severity levels: CRITICAL > ERROR > WARNING > INFO\n\n"
                           "At the end, provide a summary with counts per severity.\n\n");

    if (target && *target) {
        strbuf_append_str(&sb, "## Review Target\n\n");
        strbuf_append_str(&sb, target);
        strbuf_append_str(&sb, "\n\n");
    }

    if (focus && *focus) {
        strbuf_append_str(&sb, "## Review Focus\n\n");
        strbuf_append_str(&sb, "Pay special attention to: ");
        strbuf_append_str(&sb, focus);
        strbuf_append_str(&sb, "\n\n");
    }

    strbuf_append_str(&sb, "Start by reading the relevant files, then provide your "
                           "structured review findings.");

    return strbuf_detach(&sb);
}

char *build_security_review_prompt(const char *target) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "You are performing a **security-focused** code review. "
                           "Check for:\n\n"
                           "- Command injection vulnerabilities\n"
                           "- Path traversal attacks\n"
                           "- SQL/NoSQL injection\n"
                           "- XSS and CSRF vulnerabilities\n"
                           "- Insecure deserialization\n"
                           "- Hardcoded secrets or credentials\n"
                           "- Insufficient input validation\n"
                           "- Insecure cryptographic practices\n"
                           "- Race conditions and TOCTOU bugs\n"
                           "- Privilege escalation possibilities\n\n"
                           "Rate each finding as CRITICAL, ERROR, WARNING, or INFO.\n"
                           "Use the structured review format with severity, file, line numbers, "
                           "description, and remediation suggestion.\n\n");

    if (target && *target) {
        strbuf_append_str(&sb, "## Target\n\n");
        strbuf_append_str(&sb, target);
        strbuf_append_str(&sb, "\n\n");
    }

    strbuf_append_str(&sb, "Read the relevant files and provide your security audit findings.");

    return strbuf_detach(&sb);
}

int review_parse_severity_tag(const char *line) {
    if (!line)
        return -1;
    if (strstr(line, "[CRITICAL]"))
        return REVIEW_CRITICAL;
    if (strstr(line, "[ERROR]"))
        return REVIEW_ERROR;
    if (strstr(line, "[WARNING]"))
        return REVIEW_WARNING;
    if (strstr(line, "[INFO]"))
        return REVIEW_INFO;
    return -1;
}

int review_parse_file_line(const char *line, char *path, size_t path_sz, int *start, int *end) {
    if (!line || !path)
        return -1;

    const char *p = strstr(line, "**File:**");
    if (p) {
        p += 9;
    } else {
        p = strstr(line, "File:");
        if (!p)
            return -1;
        p += 5;
    }
    while (*p == ' ')
        p++;

    const char *pend = p;
    while (*pend && *pend != ' ' && *pend != '(' && *pend != '\n')
        pend++;

    size_t plen = (size_t)(pend - p);
    if (plen >= path_sz)
        plen = path_sz - 1;
    memcpy(path, p, plen);
    path[plen] = '\0';

    *start = 0;
    *end = 0;

    const char *paren = strchr(pend, '(');
    if (paren) {
        const char *lines_kw = strstr(paren, "lines ");
        if (!lines_kw)
            lines_kw = strstr(paren, "line ");
        if (lines_kw) {
            while (*lines_kw && (*lines_kw < '0' || *lines_kw > '9'))
                lines_kw++;
            if (*lines_kw) {
                *start = nc_safe_atoi(lines_kw, 0);
                const char *dash = strchr(lines_kw, '-');
                if (dash)
                    *end = nc_safe_atoi(dash + 1, 0);
                else
                    *end = *start;
            }
        }
    }
    return 0;
}
