#!/usr/bin/env python3
"""Autoformat weapon script .txt files using tabs.

The game's script parser requires tabs as separators, so all alignment is
done with tab characters only (assuming tab stops every 4 columns):

1. Comment lines (starting with ``//``) are left alone, except that
   trailing whitespace is trimmed.
2. Within every contiguous block of non-comment, non-blank lines, the
   whitespace-separated arguments are aligned into vertical columns by
   separating them with tabs. Indentation is converted to tabs as well.
3. Trailing whitespace is trimmed from every line.

Quoted strings ("...") count as a single argument, even if they contain
whitespace. Lines that are continuations of a quoted string spanning
multiple lines are kept fully verbatim and end the current alignment
block. Line endings (CRLF/LF) and the presence of a final newline are
preserved per file.

Usage:
    autoformat.py [--check] [--tab-width N] FILE [FILE ...]
"""

import argparse
import sys
from pathlib import Path

COMMENT = "//"
WHITESPACE = " \t"


def is_comment(line):
    """Return True if the line is a full-line comment."""
    return line.lstrip(WHITESPACE).startswith(COMMENT)


def tokenize(line):
    """Split a line's content into argument tokens.

    Returns ``(tokens, in_string)`` where ``in_string`` is True if the line
    ends inside an unterminated quoted string, which then continues on the
    following line(s). A trailing ``//`` comment outside of a string is
    returned verbatim as the last token.
    """
    tokens = []
    i, n = 0, len(line)
    while i < n:
        # Skip the whitespace separating tokens.
        if line[i] in WHITESPACE:
            i += 1
            continue
        # Trailing comment: keep the rest of the line as one token.
        if line.startswith(COMMENT, i):
            tokens.append(line[i:])
            return tokens, False
        start = i
        # Consume one token; quoted sections may contain whitespace.
        while i < n and line[i] not in WHITESPACE:
            if line[i] == '"':
                end = line.find('"', i + 1)
                if end == -1:
                    # Unterminated string: it swallows the rest of the line.
                    tokens.append(line[start:])
                    return tokens, True
                i = end + 1
            else:
                i += 1
        tokens.append(line[start:i])
    return tokens, False


def align_block(block, tab_width):
    """Align the tokens of a contiguous block of code lines using tabs.

    ``block`` is a list of ``(indent_tabs, tokens)`` tuples. Lines are
    aligned against other lines in the block that share the same
    indentation, so e.g. the indented key/value lines inside ``{ ... }``
    form their own columns independent from top-level lines.

    Every column starts on a tab stop. A column is as wide as needed to
    fit its longest token plus at least one tab, rounded up to the next
    tab stop.
    """
    # Width (in tab stops) of every column per indentation level. The last
    # token of each line needs no trailing separator, so it is ignored.
    stops = {}
    for indent, tokens in block:
        col_stops = stops.setdefault(indent, [])
        for col, token in enumerate(tokens[:-1]):
            if col == len(col_stops):
                col_stops.append(0)
            # A tab right after the token always advances to the next tab
            # stop, i.e. the token occupies len // tab_width + 1 stops.
            needed = len(token.expandtabs(tab_width)) // tab_width + 1
            col_stops[col] = max(col_stops[col], needed)

    out = []
    for indent, tokens in block:
        col_stops = stops[indent]
        parts = []
        for col, token in enumerate(tokens[:-1]):
            # Tabs needed to get from the token's end to the column's end;
            # the first tab only completes the token's partial tab stop.
            used = len(token.expandtabs(tab_width)) // tab_width
            parts.append(token + "\t" * (col_stops[col] - used))
        parts.append(tokens[-1])
        out.append("\t" * indent + "".join(parts))
    return out


def format_text(text, tab_width=4):
    """Return the formatted version of a file's text content."""
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.replace("\r\n", "\n").split("\n")

    out = []
    block = []  # pending (indent_tabs, tokens) of the current code block
    in_string = False  # inside a quoted string spanning multiple lines

    def flush():
        out.extend(align_block(block, tab_width))
        block.clear()

    for line in lines:
        # Continuation of a multi-line string: keep the string contents
        # fully verbatim, only look for where the string closes.
        if in_string:
            out.append(line)
            close = line.find('"')
            if close != -1:
                # Anything after the closing quote is not reformatted, but
                # a further unterminated string would continue it again.
                in_string = line[close + 1:].count('"') % 2 == 1
            continue

        line = line.rstrip()

        if not line or is_comment(line):
            flush()
            out.append(line)
            continue

        # Measure the indentation visually and round it up to whole tabs,
        # so e.g. an indent of three spaces becomes a single tab.
        content = line.lstrip(WHITESPACE)
        indent_width = len(line[: len(line) - len(content)].expandtabs(tab_width))
        indent = -(-indent_width // tab_width)

        tokens, in_string = tokenize(content)
        block.append((indent, tokens))
        if in_string:
            # The string continues on the next lines, which are kept
            # verbatim, so this block ends here.
            flush()

    flush()
    return eol.join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="+", type=Path, help="files to format")
    parser.add_argument(
        "--check",
        action="store_true",
        help="only report files that would change; exit 1 if any would",
    )
    parser.add_argument(
        "--tab-width",
        type=int,
        default=4,
        help="tab stop width used for alignment (default: 4)",
    )
    args = parser.parse_args()

    changed = []
    for path in args.files:
        # latin-1 round-trips arbitrary bytes, so no content gets mangled.
        # newline="" disables newline translation to preserve CRLF.
        with open(path, encoding="latin-1", newline="") as f:
            original = f.read()
        formatted = format_text(original, args.tab_width)
        if formatted == original:
            continue
        changed.append(path)
        if args.check:
            print(f"would reformat {path}")
        else:
            with open(path, "w", encoding="latin-1", newline="") as f:
                f.write(formatted)
            print(f"reformatted {path}")

    if args.check and changed:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
