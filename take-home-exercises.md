# Take-Home Exercise — Desktop Application Developer

## About This Exercise

**Our goal** is to see how you use AI-powered development tools (e.g., ChatGPT, Claude, GitHub Copilot, Cursor, agentic code generators, etc.) and what results you achieve. What we care about is your problem-solving approach and engineering skills: how you instruct and guide AI toward a working system, how you plan, what decisions you make, what the resulting code quality and architecture look like. These are the things we will also discuss in the follow-up live interview.

**Time budget:** Flexible — but we don't expect you to spend more than 3–4 hours on this. We respect your time, so don't overengineer this. A focused, well-considered solution beats a sprawling one. We don't expect to see everything implemented production-ready in such a short time, but we do expect a working prototype that demonstrates your approach.

**Technology choice is yours.** Pick whatever language, framework, and tools you are most productive with. We do ask that your solution is **cross-platform**: it should be able to run on Windows, macOS, and Linux, or at minimum provide clear instructions for building on each. Feel free to use any framework or library that makes sense for your approach. If you choose a stack that only supports a subset of platforms, explain your reasoning.

## What We Expect

We want you to use AI tools throughout the process — for project initialization, writing code, testing, debugging, all of it. This is not a trick or a trap: effective use of AI is the core of what we are evaluating. That said, we also expect you to understand and own the code you submit. Be prepared to explain any part of it in the follow-up interview.

## What to Submit

Work in this GitHub repository. The repository should contain:
   - **Source code**
   - **Documentation in README.md**: covering how to install, build, run, and use the application.
   - **AI conversation log**: attach your raw AI conversation transcripts to the repository (as `.md` or `.txt` files, or as links to shared conversations). Do not censor or polish these — we are interested in how you asked questions, how you had the AI fix bugs, and how you arrived at the solution step by step. Don't be ashamed of phrasing, typos, mistakes, or failed attempts, we value honesty and learning.

## How We Evaluate

| Criteria | What we're looking for |
|---|---|
| **AI tool usage** | Thoughtful delegation, effective prompting, good context management, well chosen tools, appropriate skepticism toward AI output |
| **Engineering judgment** | Sensible architecture, edge case awareness, pragmatic tradeoffs, performance considerations |
| **Code quality** | Readable, consistent, maintainable, well-structured - whether human-written or AI-generated |
| **Process & communication** | Honest conversation logs, clear reasoning, good documentation |
| **Completeness** | Core requirements working; stretch goals are welcome but not at the expense of stability |

---

## The Task: Build a Log File Viewer

Build a cross-platform desktop application for viewing and analyzing log files. Think of it as a lightweight, fast alternative to opening huge log files in a text editor.

### Core Requirements

- **Open and display log files**: The user can open a `.log` or `.txt` file and see its contents in a scrollable, read-only view. It should handle files up to at least 200MB without choking.
- **Filtering**: A text input that filters lines in real time (or near-real-time). The user types a string, and only matching lines are shown. Support both plain text and regex matching (with a toggle).
- **Log level detection**: Automatically detect and color-code common log levels (ERROR, WARN, INFO, DEBUG, TRACE) regardless of the specific format — some logs use `[ERROR]`, some use `level=error`, some use `E/` (Android-style), etc.
- **Statistics panel**: Show a summary — total line count, breakdown by detected log level, and a simple timeline showing error/warning frequency over time (e.g., a basic bar chart or sparkline grouped by minute or hour).

### Areas That Require Careful Thought

These are not hidden traps — they are the parts of the work where senior engineering judgment matters most. We would much rather see a thoughtful README or RFC explaining how you would solve a problem than a half-finished attempt at implementing it.

- **Performance with large files**: A 200MB log file might have millions of lines.
- **Log format parsing**: There is no universal log format. Your level detection and timestamp parsing need to be resilient to unexpected formats. Think about what happens when a log file mixes formats or has multi-line entries (stack traces, for instance).
- **Filtering performance**: Regex filtering across millions of lines needs to be snappy. Consider your approach to indexing or incremental search.
- **File encoding**: Log files might be UTF-8, Latin-1, or something else. Handle this without garbling the output.
- **Cross-platform consistency**: Make sure the UI behaves and looks reasonable on all target platforms, not just the one you develop on.

### Example Log Files

The `examples/` folder contains sample log files you can use for development and testing. Each file exercises a different aspect of the problem:

| File | Format | What it tests |
|---|---|---|
| `simple-webserver.log` | Standard `[LEVEL] message` | Clean, consistent format — good starting point |
| `android-logcat.log` | Android `E/`/`W/`/`I/`/`D/`/`V/` prefixes | Format-agnostic log level detection |
| `mixed-multiline.log` | Java logs with stack traces + mixed formats | Multi-line entries, format inconsistencies, malformed lines |
| `high-volume-service.log` | JSON log lines (~5K lines) | Filtering performance, statistics/timeline features |
| `latin1-legacy.log` | ISO-8859-1 (Latin-1) encoded | Non-UTF-8 file encoding handling |

**Generating large files for performance testing:**

```sh
# Generate a 200MB log file (default)
./examples/generate-large-log.sh

# Generate a specific size
./examples/generate-large-log.sh 500 my-large-test.log
```

The generator produces realistic mixed-format output (structured, JSON, and key-value lines) with periodic error bursts, so the statistics timeline has interesting patterns to display.

### Stretch Goals (only if you have time)

- Multiple tabs for opening several log files at once
- Export filtered results to a new file
- Bookmarking or pinning specific lines for quick reference
- Tail mode: watch a live log file and auto-scroll as new lines are appended

---

## A Note to Candidates

We designed this exercise to reflect real-world work. Your AI conversation log is genuinely the most interesting part of the submission for us. We want to understand how you think, how you collaborate with AI tools, and how you navigate the inevitable moments where the AI produces something wrong or incomplete. Honest, unfiltered logs — including dead ends and failed attempts — are more impressive to us than a polished facade.

Good luck, and have fun with it.
