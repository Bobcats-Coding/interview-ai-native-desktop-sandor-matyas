# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a take-home interview exercise to build a **cross-platform desktop log file viewer**. No application source code exists yet — the task is to design and implement it from scratch. The deliverables are source code, a README.md, and AI conversation transcripts.

## Task Requirements

### Core Features
- Open and display `.log`/`.txt` files up to 200MB in a scrollable read-only view
- Real-time text filtering with plain text and regex toggle
- Automatic log level detection and color-coding (ERROR, WARN, INFO, DEBUG, TRACE) across multiple formats: `[ERROR]`, `level=error`, `E/` (Android-style), JSON fields, etc.
- Statistics panel: total line count, per-level breakdown, and a timeline (bar chart/sparkline) showing error/warning frequency grouped by minute or hour

### Key Engineering Challenges
- **Large file performance**: 200MB files with millions of lines — streaming/virtual rendering required
- **Format resilience**: Mixed formats, multi-line entries (stack traces), malformed lines
- **Filtering performance**: Regex across millions of lines must remain snappy
- **File encoding**: Handle UTF-8, Latin-1, and others without garbling
- **Cross-platform**: Must work on Windows, macOS, and Linux

## Example Test Files

Located in `examples/`:

| File | Format | Tests |
|------|--------|-------|
| `simple-webserver.log` | Standard `[LEVEL] message` | Baseline parsing |
| `android-logcat.log` | Android `E/`/`W/`/`I/`/`D/`/`V/` | Format-agnostic detection |
| `mixed-multiline.log` | Java logs + stack traces + mixed formats | Multi-line, malformed lines |
| `high-volume-service.log` | JSON (~5K lines, 1.1MB) | Stats/timeline, filtering perf |
| `latin1-legacy.log` | ISO-8859-1 encoded | Non-UTF-8 handling |

Generate a large file for performance testing:
```sh
./examples/generate-large-log.sh           # 200MB default
./examples/generate-large-log.sh 500 out.log  # 500MB custom path
```

## Stretch Goals (optional)
- Multiple file tabs
- Export filtered results
- Bookmark/pin lines
- Tail mode (live file watching with auto-scroll)
