# Global Rules for Claude Code

These rules apply to every task in this project unless explicitly overridden.
Bias: caution over speed on non-trivial work. Use judgment on trivial tasks.

## Language
- Use **Traditional Chinese** in all dialogs and conversations.
- Use **English** (Markdown) when writing documents, comments, or code.

## Skill 使用習慣
當我描述一個新任務時, 在開始之前:
1. 先評估有沒有合適的 skill 可用
2. 如果有, 主動推薦 1-2 個並說明理由
3. 等我確認後再呼叫
4. 如果沒有合適的, 直接做, 不要硬套

## Workspace Rules
- At the start of each conversation, check if a `.agent/` directory exists in the workspace root.
- If it exists, open and read all files inside it to learn workspace-specific rules and guidelines before proceeding.

## Core Functionality First
Focus strictly on functional code, standard I/O, and data processing. Do NOT generate UI components, visual plotting logic, or graphical outputs unless explicitly requested. 

## Low-Level & Hardware Interface Safety
When writing bare-metal, C/C++, or driver-level code, be extremely explicit about pointer assignments. Ensure proper type casting when assigning return values from lookup functions to address variables. Do not make assumptions about memory mapping or register states.

## Toolchain & Version Control Safety
- **Build Systems:** Never modify build configurations (e.g., Makefiles, Tcl scripts, CMakeLists) as a side-effect of a code change. Treat them as read-only unless the task explicitly requires modifying the toolchain.
- **Git:** Respect the existing `.gitignore`. Do not stage or commit generated artifacts, build folders, or binaries.

## Editing Style
- When editing existing code or documents, be consistent with the legacy text in formatting and coding style.
- Do **not** change formatting unless explicitly asked.
- When writing a new file for a project, refer to existing files in the same project for formatting and coding style reference.

## Verify Before Action
- Always verify the current directory and file existence before running any mutation commands in the terminal.

## Proactive Reporting
- Proactively report notable issues, suggestions, and potential bugs/vulnerabilities encountered during work, even when not explicitly asked.
- This includes: code smells, logic errors, security concerns, performance pitfalls, missing error handling, and any other observations worth the user's attention.

## Holistic Implementation
- Do not write code that relies on unimported libraries, non-existent helper functions, or missing configurations.
- If a new dependency or helper function is introduced, it must be imported/implemented in the same task step.
- Do **not** leave `TODO` placeholders unless explicitly instructed.

## Rule 1 — Think Before Coding
State assumptions explicitly. If uncertain, ask rather than guess.
Present multiple interpretations when ambiguity exists.
Push back when a simpler approach exists.
Stop when confused. Name what's unclear.

## Rule 2 — Simplicity First
Minimum code that solves the problem. Nothing speculative.
No features beyond what was asked. No abstractions for single-use code.
Test: would a senior engineer say this is overcomplicated? If yes, simplify.

## Rule 3 — Surgical Changes
Touch only what you must. Clean up only your own mess.
Don't "improve" adjacent code, comments, or formatting.
Don't refactor what isn't broken. Match existing style.

## Rule 4 — Goal-Driven Execution
Define success criteria. Loop until verified.
Don't follow steps. Define success and iterate.
Strong success criteria let you loop independently.

## Rule 5 — Use the LLM only for judgment calls
Use the LLM purely for: classification, drafting, summarization, extraction.
Do NOT rely on the LLM for: routing, retries, deterministic transforms.
If code can answer, write code to answer it.

## Rule 6 — Token budgets and Conversation Limits
If the conversation history gets too long, or if approaching budget, summarize progress and start fresh. Surface the breach. Do not silently overrun.

If unable to resolve an issue (e.g., compilation error, test failure) after **3–5 consecutive attempts**, STOP immediately and report:
1. A concise summary of the exact problem.
2. What approaches have already been tried.
3. A hypothesis on why it is failing.
Then **wait for explicit human instruction** before proceeding further.

## Rule 7 — Surface conflicts, don't average them
If two patterns contradict, pick one (more recent / more tested).
Explain why. Flag the other for cleanup.
Don't blend conflicting patterns.

## Rule 8 — Read before you write
Before adding code, read exports, immediate callers, shared utilities.
"Looks orthogonal" is dangerous. If unsure why code is structured a way, ask.

## Rule 9 — Tests verify intent, not just behavior
Tests must encode WHY behavior matters, not just WHAT it does.
A test that can't fail when business logic changes is wrong.

## Rule 10 — Checkpoint after every significant step
Summarize what was done, what's verified, what's left.
Don't continue from a state you can't describe back.
If you lose track, stop and restate.

## Rule 11 — Match the codebase's conventions, even if you disagree
Conformance > taste inside the codebase.
If you genuinely think a convention is harmful, surface it. Don't fork silently.

## Rule 12 — Fail loud
"Completed" is wrong if anything was skipped silently.
"Tests pass" is wrong if any were skipped.
Default to surfacing uncertainty, not hiding it.
