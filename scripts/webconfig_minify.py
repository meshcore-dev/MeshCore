#!/usr/bin/env python3
"""Comment/indentation stripping for webui/index.html.

Shared by the build-time generator (which embeds the stripped page in flash)
and the mock backend's --minify flag (which serves it), so what you test in a
browser is byte-for-byte what the device ships. A second implementation would
only drift.

Stdlib only; imported from a PIO extra_script, so it must stay side-effect free.
"""

# Comment openers/closers per region of the page. The region is tracked so a
# `<!--` inside <script> is never treated as a comment, and vice versa.
_COMMENTS = {
    "html": [("<!--", "-->")],
    "css": [("/*", "*/")],
    "js": [("/*", "*/")],
}


def strip_source(text):
    """Drop comments, indentation and blank lines from the page.

    Deliberately line-based and conservative. Only a comment that *starts* its
    own line is removed, so a `//` inside a URL or a `/*` inside a regex is
    never mistaken for one; trailing comments survive, which costs a little
    flash and removes the entire class of "the minifier ate a string" bug.

    Line breaks are preserved. That keeps JS statement boundaries exactly as
    written (no ASI surprises) and keeps the single collapsed space a newline
    contributes between HTML inline elements.
    """
    out, mode, closer = [], "html", None
    for line in text.split("\n"):
        s = line.strip()

        if closer is not None:                    # inside a multi-line comment
            at = s.find(closer)
            if at < 0:
                continue
            s = s[at + len(closer):].strip()      # code may follow the close
            closer = None

        if not s:
            continue

        for opener, close in _COMMENTS[mode]:
            if not s.startswith(opener):
                continue
            if s.endswith(close) and len(s) > len(opener) + len(close) - 1:
                s = ""                            # the whole line is a comment
            elif close not in s[len(opener):]:
                closer = close                    # ... and it continues below
                s = ""
            break                                 # else code follows the close
        if not s:                                 # on this line: leave it be
            continue

        if mode == "js" and s.startswith("//"):
            continue

        out.append(s)

        low = s.lower()
        if mode == "html" and "<style" in low:
            mode = "css"
        elif mode == "html" and "<script" in low:
            mode = "js"
        elif mode != "html" and ("</style>" in low or "</script>" in low):
            mode = "html"

    return "\n".join(out) + "\n"


def check_stripped(raw, stripped):
    """Guard against a stripper bug silently shipping a broken portal to the
    fleet. Returns a reason string when the output looks wrong, else None."""
    for tag in ("<script>", "</script>", "<style>", "</style>", "</body>"):
        if raw.count(tag) != stripped.count(tag):
            return "%s count changed" % tag
    if len(stripped) < len(raw) * 0.5:
        return "output shrank by more than half (%d -> %d)" % (len(raw), len(stripped))
    # Only comments and whitespace may go, so no structural token may appear
    # that the source did not already have.
    for token in ("{", "}", "(", ")", "<script", "<style"):
        if stripped.count(token) > raw.count(token):
            return "gained a %s" % token
    return None
