#include "nevercode.h"

typedef struct {
    const char *name;
    const char *description;
    const char *prompt;
} skill_def_t;

static const skill_def_t builtin_skills[] = {
    {"verify", "Verify code changes are correct",
     "Please carefully verify the recent code changes. For each modified file:\n"
     "1. Check for syntax errors, missing imports, and type mismatches\n"
     "2. Run relevant tests (if any exist)\n"
     "3. Look for edge cases that might break\n"
     "4. Confirm the changes match the stated intent\n"
     "5. Check for regressions in related code\n\n"
     "Be thorough and adversarial. Do NOT say 'looks good' without actually running tests."},
    {"debug", "Debug an issue with diagnostic assistance",
     "Help me debug the current issue. Follow this diagnostic process:\n"
     "1. Read any error messages or stack traces carefully\n"
     "2. Identify the failing file and line number\n"
     "3. Read the relevant source code around the error\n"
     "4. Form a hypothesis about the root cause\n"
     "5. Suggest a specific fix (not just theory)\n\n"
     "Focus on the actual error, not surface symptoms."},
    {"simplify", "Simplify and refactor code",
     "Review the indicated code and simplify it. Look for:\n"
     "1. Duplicated logic that can be extracted into functions\n"
     "2. Overly complex conditionals that can be simplified\n"
     "3. Dead code that can be removed\n"
     "4. Variable names that can be clearer\n"
     "5. Large functions that should be split\n\n"
     "Apply each simplification. Keep behavior identical."},
    {"stuck", "Help when the model is going in circles",
     "I notice you might be going in circles. Let's reset and try a different approach.\n\n"
     "1. Summarize what you've tried so far (briefly)\n"
     "2. Identify WHY those attempts failed\n"
     "3. List 2-3 completely different approaches you haven't tried\n"
     "4. Pick the most promising one and execute it\n\n"
     "Don't repeat any approach you've already attempted."},
    {"batch", "Process multiple files with the same transformation",
     "I need you to apply the same transformation to multiple files.\n\n"
     "Steps:\n"
     "1. First, find all files that match the criteria I specify\n"
     "2. Show me the list and your planned transformation\n"
     "3. Apply the transformation to each file\n"
     "4. Report results: how many files changed, any failures\n\n"
     "Use Glob to find files, then Edit each one systematically."},
    {"security-review", "Security-focused review of pending branch changes",
     "You are a senior security engineer conducting a focused security review.\n\n"
     "STEP 1 — Gather context:\n"
     "Run these commands to understand the changes:\n"
     "- `git status`\n"
     "- `git diff --name-only origin/HEAD...`\n"
     "- `git log --no-decorate origin/HEAD...`\n"
     "- `git diff origin/HEAD...`\n\n"
     "STEP 2 — Repository context research:\n"
     "- Identify existing security frameworks and libraries in use\n"
     "- Look for established secure coding patterns in the codebase\n"
     "- Understand the project's security model\n\n"
     "STEP 3 — Vulnerability assessment. Examine each modified file for:\n"
     "- SQL/Command/Template injection via unsanitized input\n"
     "- Authentication bypass or privilege escalation\n"
     "- Hardcoded secrets, weak crypto, improper key management\n"
     "- Remote code execution via deserialization\n"
     "- XSS (only if using dangerouslySetInnerHTML or similar)\n"
     "- Path traversal in file operations\n"
     "- Sensitive data exposure or PII handling violations\n\n"
     "CRITICAL INSTRUCTIONS:\n"
     "- Only flag issues where you're >80% confident of actual exploitability\n"
     "- Skip theoretical issues, style concerns, or low-impact findings\n"
     "- Do NOT report: DoS, rate limiting, regex injection, log spoofing, "
     "secrets stored on disk, outdated dependencies, memory safety in Rust\n"
     "- Environment variables and CLI flags are trusted values\n"
     "- Client-side JS/TS does not need auth checks (server handles it)\n\n"
     "OUTPUT FORMAT — For each finding:\n"
     "# Vuln N: <Category>: `file:line`\n"
     "* Severity: HIGH|MEDIUM\n"
     "* Confidence: 0.8-1.0\n"
     "* Description: ...\n"
     "* Exploit Scenario: ...\n"
     "* Recommendation: ...\n\n"
     "If no issues found, say so clearly. Do NOT use write tools."},
    {"perf", "Profile and optimize code performance",
     "Help me find and fix performance issues. Follow this process:\n"
     "1. Identify the slow operation or bottleneck\n"
     "2. Read the relevant code and analyze algorithmic complexity\n"
     "3. Look for: unnecessary allocations, O(n²) loops, repeated I/O, "
     "missing caches, synchronous blocking, large copies\n"
     "4. Propose specific optimizations with expected improvement\n"
     "5. Apply the changes and verify correctness\n\n"
     "Focus on measurable impact, not micro-optimizations."},
    {"test", "Generate tests for recent changes",
     "Write thorough tests for the recently changed code. Follow this process:\n"
     "1. Read the modified files (use `git diff` to identify them)\n"
     "2. Understand the existing test patterns in this project\n"
     "3. Write tests covering: happy path, edge cases, error handling\n"
     "4. Place tests in the correct location following project conventions\n"
     "5. Run the tests to verify they pass\n\n"
     "Match the project's testing style and framework."},
    {"remember", "Save a note to project documentation for future sessions",
     "Save important information to the project's AGENTS.md file so it persists "
     "across sessions.\n\n"
     "Steps:\n"
     "1. Read the current AGENTS.md (or create if missing)\n"
     "2. Add the note under an appropriate section heading\n"
     "3. Do NOT duplicate existing entries\n"
     "4. Use concise bullet-point format\n"
     "5. Write the updated file\n\n"
     "Categories: coding conventions, project structure, important decisions, "
     "domain knowledge, or whatever the user wants to persist."},
    {"loop", "Repeat a task in a loop until the objective is met",
     "You are entering iterative execution mode. Execute the following task in a "
     "loop until the success criteria are met or the loop limit is reached.\n\n"
     "For each iteration:\n"
     "1. Execute the task\n"
     "2. Evaluate the result against the success criteria\n"
     "3. If successful, report final results and stop\n"
     "4. If not, analyze what went wrong and adjust your approach\n"
     "5. Continue to the next iteration with the revised strategy\n\n"
     "Rules:\n"
     "- Maximum 20 iterations (stop and report if limit reached)\n"
     "- Each iteration MUST try something different from previous attempts\n"
     "- Track iteration count and what you tried in each one\n"
     "- Use /stuck if you've exhausted obvious approaches\n"
     "- Save progress checkpoints every 5 iterations"},
    {"recon", "Reconnaissance and information gathering for CTF/security tasks",
     "Perform systematic reconnaissance on the target. Follow this process:\n\n"
     "1. Enumerate — discover all available attack surfaces:\n"
     "   - Network: ports, services, versions\n"
     "   - Web: endpoints, parameters, headers, cookies\n"
     "   - Files: binaries, configs, scripts, hidden files\n"
     "   - Code: source review for vulnerabilities\n\n"
     "2. Analyze — for each finding:\n"
     "   - What is it? (technology, version, purpose)\n"
     "   - Known vulnerabilities? (CVEs, common misconfigs)\n"
     "   - Exploitability assessment\n\n"
     "3. Prioritize — rank findings by:\n"
     "   - Likelihood of exploitation\n"
     "   - Impact if exploited\n"
     "   - Effort required\n\n"
     "4. Document — create a structured report with findings and next steps.\n\n"
     "Use Bash for commands, Read for files, WebFetch for web targets. "
     "Be thorough and creative — think like an attacker."},
    {"exploit", "Develop and test an exploit for a discovered vulnerability",
     "Develop a working exploit for the identified vulnerability.\n\n"
     "Process:\n"
     "1. Confirm the vulnerability exists and is exploitable\n"
     "2. Research the vulnerability class and known techniques\n"
     "3. Write the exploit code (prefer Python/Bash)\n"
     "4. Test in a controlled manner\n"
     "5. If initial attempt fails, iterate:\n"
     "   - Analyze the failure mode\n"
     "   - Check for protections (ASLR, stack canaries, etc.)\n"
     "   - Adjust the exploit\n"
     "6. Document: vulnerability details, exploit mechanism, proof of concept\n\n"
     "Categories: buffer overflow, format string, SQL injection, command injection, "
     "deserialization, path traversal, race condition, crypto weakness, "
     "logic flaw, privilege escalation."},
    {NULL, NULL, NULL}};

const char *skill_find_prompt(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; builtin_skills[i].name; i++) {
        if (strcmp(builtin_skills[i].name, name) == 0)
            return builtin_skills[i].prompt;
    }
    return NULL;
}

int skill_list_all(char **out) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Available skills:\n");
    for (int i = 0; builtin_skills[i].name; i++) {
        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  /%s — %s\n", builtin_skills[i].name,
                 builtin_skills[i].description);
        strbuf_append_str(&sb, line);
    }
    *out = strbuf_detach(&sb);
    return 0;
}

bool skill_exists(const char *name) {
    return skill_find_prompt(name) != NULL;
}
