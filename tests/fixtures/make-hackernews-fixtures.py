#!/usr/bin/env python3
"""Writes the Hacker News HTTP fixtures of the UI and unit tests (docs/apps/hackernews.md), in the shape the two public
APIs answer (checked against the live services on 2026-11-18):

  hacker-news.firebaseio.com/v0/<list>.json      the ids of a list, in the site's order
  hacker-news.firebaseio.com/v0/item/<id>.json   one story: by, descendants, kids (the site's order), score, time, title, url, text
  hn.algolia.com/api/v1/search?tags=...          hits with objectID and the attributes asked for; nbHits
  r.jina.ai (POST {"url": ...})                  a page as text: "Title: ...", "Markdown Content:", the page as Markdown
  markdown.new (POST {"url": ...})               the second reader: JSON success, title, content (a YAML front matter, Markdown)

The stories, names and comments are made up: nothing of the real site is copied. Times count back from the moment the UI
scripts start at, so the ages on the screenshots stay the same.

  python3 -I tests/fixtures/make-hackernews-fixtures.py tests/fixtures/http
"""
import json, os, sys

NOW = 1768480440  # 2026-01-15 12:34:00 UTC: when the UI scripts start (tools/sim-run.sh)
ORDER = "hacker-news.firebaseio.com"
SEARCH = "hn.algolia.com"
STORY_ATTRS = "&attributesToRetrieve=title,url,author,points,num_comments,created_at_i&attributesToHighlight=none"
COMMENT_ATTRS = "&attributesToRetrieve=author,comment_text,parent_id,created_at_i&attributesToHighlight=none"
COMMENTS_MAX = 1000


def fnv1a(s):
    h = 2166136261
    for c in s.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return "%08x" % h


def esc(s):
    """Text as the site's HTML has it."""
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("'", "&#x27;").replace('"', "&quot;").replace("/", "&#x2F;")


# (id, title, url, by, points, comments, minutes old)
TOP = [
    (38250001, "A file system that fits on a postcard", "https://www.example.com/postcard-fs", "marit", 412, 14, 190),
    (38250002, "Ask HN: What did you stop automating?", "", "tobiasv", 233, 3, 260),
    (38250003, "Show HN: A pocket calculator that prints its own manual", "https://calc.example.org/manual", "edsger_w", 178, 0, 95),
    (38250004, "The long tail of time zone bugs", "https://blog.example.net/2023/time-zones", "halfnote", 356, 97, 410),
    (38250005, "E-ink at 120 Hz: what the waveforms really do", "https://panels.example.com/waveforms", "inkwell", 291, 64, 330),
    (38250006, "Why our compiler got slower when we made it parallel", "https://lang.example.dev/parallel-slower", "grace_m", 240, 58, 505),
    (38250007, "Reading a 1978 teletext decoder schematic", "https://museum.example.org/teletext-1978", "ceefax", 121, 22, 615),
    (38250008, "Postmortem: the day every clock in the building jumped a leap second early", "https://status.example.com/postmortems/leap-second", "ntp_enjoyer", 512, 203, 720),
    (38250009, "A tiny Forth for the ESP32-S3", "https://github.example.com/tinyforth/s3", "stackjuggler", 97, 31, 150),
    (38250010, "Bicycle lanes and the mathematics of queueing", "https://www.example.nl/fietspaden-wachtrijen", "anneke", 143, 76, 840),
    (38250011, "SQLite as an application file format, ten years on", "https://db.example.org/ten-years", "rowid", 388, 112, 960),
    (38250012, "The paper notebook that outlived three note-taking apps", "https://notes.example.com/paper", "margin_notes", 265, 140, 1100),
    (38250013, "An interactive guide to rope data structures", "https://ropes.example.dev/", "tess", 89, 12, 205),
    (38250014, "Show HN: I mapped every public bench in my town", "https://benches.example.org", "sitdown", 310, 88, 1230),
    (38250015, "What a 4-bit CPU taught me about caches", "https://retro.example.com/4bit-caches", "nibble", 154, 41, 1320),
    (38250016, "Font hinting is dead; long live font hinting", "https://type.example.net/hinting", "kern_pair", 201, 93, 1410),
    (38250017, "The economics of very small data centres", "https://infra.example.com/small-dc", "rackunit", 76, 29, 380),
    (38250018, "How we test firmware without the hardware", "https://embedded.example.dev/sim-first", "golden_shot", 187, 52, 1500),
    (38250019, "A week with a phone that only does text", "https://www.example.co.uk/text-phone", "plaintext", 421, 305, 1620),
    (38250020, "Ask HN: Books that changed how you debug?", "", "bisect", 164, 121, 1710),
    (38250021, "Tides, harmonics and a mechanical computer from 1872", "https://history.example.org/tide-predictor", "kelvin72", 132, 18, 1800),
    (38250022, "The surprising cost of a JSON number", "https://perf.example.com/json-number", "mantissa", 119, 67, 1890),
    (38250023, "Rewriting a 30-year-old Makefile", "https://build.example.net/makefile", "tabs_not_spaces", 98, 84, 1950),
    (38250024, "Low-power Wi-Fi: lessons from a year on one battery", "https://iot.example.com/one-battery", "duty_cycle", 276, 73, 2040),
    (38250025, "Dithering for people in a hurry", "https://graphics.example.org/dither", "bayer", 222, 35, 2130),
    (38250026, "The oldest bug in our tracker turned 21", "https://tracker.example.dev/21", "wontfix", 301, 156, 2220),
    (38250027, "Show HN: A sudoku where the clues are sums", "https://puzzles.example.com/sums", "killer_cage", 64, 19, 2310),
    (38250028, "Notes on designing for a screen that cannot scroll", "https://ui.example.net/no-scroll", "pager", 145, 47, 2400),
    (38250029, "What we learned running a mail server for 15 years", "https://mail.example.org/fifteen", "mx_record", 259, 131, 2520),
    (38250030, "Ask HN: Is it still worth learning assembly?", "", "mov_eax", 87, 142, 2640),
    (38250031, "The thirty-first story of the list, which the app does not show", "https://www.example.com/31", "overflow", 5, 1, 2700),
    (38250032, "The thirty-second", "https://www.example.com/32", "overflow", 4, 0, 2710),
    (38250033, "The thirty-third", "https://www.example.com/33", "overflow", 3, 0, 2720),
    (38250034, "The thirty-fourth, which is not even asked for", "https://www.example.com/34", "overflow", 2, 0, 2730),
]
JOB = 38259999  # a job advert: in the list of ids, unknown to the search

NEW = [
    (38260001, "Show HN: A clock that only shows the tide", "https://tide.example.org", "low_water", 1, 0, 2),
    (38260002, "The case for boring file names", "https://files.example.com/boring", "untitled2", 2, 0, 5),
    (38260003, "Measuring the speed of light with a chocolate bar", "https://physics.example.net/chocolate", "standing_wave", 4, 1, 9),
    (38260004, "Ask HN: How do you keep a paper log?", "", "bound_book", 1, 0, 14),
    (38260005, "A brief history of the progress bar", "https://ux.example.org/progress", "ninety_nine", 3, 2, 21),
    (38260006, "Notes from a conference with no slides", "https://talks.example.dev/no-slides", "whiteboard", 7, 4, 33),
    (38260007, "The smallest useful font is 5 pixels tall", "https://type.example.net/five", "kern_pair", 12, 3, 47),
    (38260008, "Why the lift always arrives full", "https://queues.example.com/lift", "anneke", 2, 0, 58),
]
ASK = [TOP[1], TOP[19], TOP[29], NEW[3]]
SHOW = [TOP[2], TOP[13], TOP[26], NEW[0]]

ASK_TEXT = ("For years I scripted everything: backups, expense reports, even watering the plants.<p>"
            "Last winter I turned a few of those off again and found I did not miss them. "
            "What did <i>you</i> stop automating, and why?")

# The comments of the first story: (id, parent, by, minutes old, html). Ids rise with time, as on the site.
S1 = TOP[0][0]
COMMENTS_1 = [
    (38250101, S1, "rowid", 181,
     "I read the whole thing twice. The trick of storing the free list <i>inside</i> the free blocks is old, "
     "but I had never seen it explained this clearly.<p>The postcard is a nice touch: it forces every structure to justify its bytes."),
    (38250102, S1, "nibble", 178,
     "Does it survive a power cut in the middle of a rename? The article is quiet about that."),
    (38250103, 38250101, "marit", 172,
     "Author here. Thank you! The free list idea is from a 1970s paper I link at the end; I only drew the pictures."),
    (38250104, 38250102, "marit", 170,
     "It does: a rename writes the new entry first and clears the old one last, so a cut leaves you with two names for "
     "one file, never none. The check at mount time removes the older one:"
     "<p><pre><code>for entry in directory:\n    if entry.block in seen:\n        drop(older(entry, seen[entry.block]))\n    seen[entry.block] = entry\n</code></pre>"),
    (38250105, 38250104, "nibble", 166,
     "Two names for one file is a fair trade. Is &quot;older&quot; decided by a counter or by a clock? A clock would worry me."),
    (38250106, 38250105, "marit", 163, "A counter, 16 bits, kept in the directory entry. No clock anywhere."),
    (38250107, 38250106, "ntp_enjoyer", 160, "As someone who has been bitten by clocks: bless you."),
    (38250108, S1, "duty_cycle", 158,
     "We ship something close to this on a sensor that wakes twice a day. Two years in the field, about 4000 units, no corrupt cards so far. "
     "The part people underestimate is wear: with so few blocks you rewrite the same ones very often.<p>"
     "Our fix was dull and it works: rotate the directory through eight blocks and keep a counter to find the newest."
     "<p>Details: <a href=\"https:&#x2F;&#x2F;iot.example.com&#x2F;wear-notes\" rel=\"nofollow\">https:&#x2F;&#x2F;iot.example.com&#x2F;wear-notes</a>"),
    (38250109, 38250108, "golden_shot", 150, "How do you test the rotation without waiting two years?"),
    (38250110, 38250109, "duty_cycle", 147,
     "A simulator that cuts power at every single write, then mounts the image and checks it. Slow, but it runs overnight."),
    (38250111, S1, "tabs_not_spaces", 141, None),  # deleted, answered
    (38250112, 38250111, "pager", 139, "I think the question was whether directories can nest. They can not; it is one flat list of 64 names."),
    (38250113, S1, "kern_pair", 120,
     "Small formats age well. I can still read the files my first program wrote, and it is older than some of my colleagues."),
    (38250114, S1, "wontfix", 96, None),  # deleted, nobody answered: left out
    (38250115, 38259000, "lost", 90, "A reply to a comment that is not among the hits: left out."),
]
KIDS_1 = [38250108, 38250101, 38250113, 38250102, 38250111, 38250114]  # the site's order, not the order of writing

S2 = TOP[1][0]
COMMENTS_2 = [
    (38250201, S2, "plaintext", 240, "My to-do list. The script that sorted it took more care than the list ever did."),
    (38250202, 38250201, "bisect", 231, "Same here. A sheet of paper has a wonderful feature: it ends."),
    (38250203, S2, "rackunit", 225, "Watering the plants. The plants disagreed with the schedule."),
]
KIDS_2 = [38250203, 38250201]

S4 = TOP[3][0]  # its comments can not be read (503)

READER = "r.jina.ai"
FALLBACK = "markdown.new"
# A site that shows the first reader a CAPTCHA: no text, a warning. The second reads it (ARTICLE_5).
CAPTCHA_5 = """Title: panels.example.com

URL Source: https://panels.example.com/waveforms

Warning: This page maybe requiring CAPTCHA, please make sure you are authorized to access this page.

Markdown Content:

"""
ARTICLE_5 = """---
title: "E-ink at 120 Hz: what the waveforms really do"
description: A panel does not refresh, it is driven.
---

# E-ink at 120 Hz: what the waveforms really do

An e-ink panel has no frame rate in the way a screen has one. What it has is a **waveform**: a table that says, for every pair of old and new gray, which voltage to put on a pixel in each of a few dozen frames.

[Subscribe to the newsletter](https://panels.example.com/subscribe)

## Frames, not refreshes

The controller runs those frames at 85 or 120 Hz. A fast update is a short table, ten frames or so, that only knows black and white; a full one is forty frames of pushing the particles back and forth until no trace of the old picture is left.

- Short tables leave a ghost.
- Long tables flash.
- Temperature changes everything: a cold panel needs longer tables.

## Why it matters

Most of what feels slow about e-ink is the choice of table, not the panel. Pick the short one for a page turn and the long one now and then, and the panel feels quick and stays clean.
"""
# The article of the first story, as the reader service answers (plain text: a few lines about the page, then Markdown).
ARTICLE_1 = """Title: A file system that fits on a postcard

URL Source: https://www.example.com/postcard-fs

Published Time: 2026-01-15T08:00:00Z

Markdown Content:
A file system that fits on a postcard
=====================================

_By Marit, January 2026_

Most file systems are described in books. This one is described on a **postcard**, and the postcard has room to spare. It stores up to 64 files on a card of 4096 blocks, survives a power cut at any moment, and its whole on-disk format is three structures.

I wrote it for a sensor that wakes twice a day, writes one line and goes back to sleep. Nothing about that job needs directories, permissions or timestamps, so there are none.

## The three structures

1.  The **superblock**: one block, written once, never changed.
2.  The **directory**: 64 entries of 32 bytes, rotated through eight blocks.
3.  The **free list**, which lives _inside_ the free blocks themselves.

That last one is the trick. A free block has nothing better to do than to hold the number of the next free block, so the list costs no space at all:

```
struct free_block {
    uint16_t next;      /* 0 = end of the list */
    uint16_t count;
};
```

Allocating a block is reading `next` from the head; freeing one is writing the old head into it.

## Surviving a power cut

> Two names for one file is a fair trade; no name for a file is not.

A rename writes the new entry first and clears the old one last. If the power goes in between, the check at mount time finds two entries for one block and drops the older one. Which is older is decided by a 16-bit counter in the entry, not by a clock: the sensor has no idea what time it is.

### What it costs

| Operation | Blocks written |
| --- | --- |
| Append a line | 1 |
| Create a file | 2 |
| Rename | 2 |

*   No write ever touches more than two blocks.
*   The directory block in use changes with every write, which spreads the wear.
    *   Eight blocks share it, so each sees an eighth of the writes.
*   Reading never writes.

* * *

## Was it worth it?

After two years and about 4000 units in the field, no card has come back corrupt. The format has not changed since the day it was printed, and I can still read the first card I ever wrote with it. Small formats age well.

The source is 412 lines of C, snake\\_case and all, and the postcard is taped above my desk.
"""


def story_hit(s):
    sid, title, url, by, points, comments, age = s
    hit = {"author": by, "created_at_i": NOW - age * 60, "num_comments": comments, "objectID": str(sid), "points": points, "title": title}
    if url:
        hit["url"] = url
    return hit


def item(s, kids=None, text=None):
    sid, title, url, by, points, comments, age = s
    out = {"by": by, "descendants": comments, "id": sid}
    if kids:
        out["kids"] = kids
    out.update({"score": points, "time": NOW - age * 60})
    if text:
        out["text"] = text
    out.update({"title": title, "type": "story"})
    if url:
        out["url"] = url
    return out


def comment_hit(c):
    cid, parent, by, age, html = c
    return {"author": by, "comment_text": html, "created_at_i": NOW - age * 60, "objectID": str(cid), "parent_id": parent}


def search(hits, per_page):
    return {"exhaustive": {"nbHits": True, "typo": True}, "exhaustiveNbHits": True, "exhaustiveTypo": True, "hits": hits,
            "hitsPerPage": per_page, "nbHits": len(hits), "nbPages": 1, "page": 0, "processingTimeMS": 1, "query": "", "serverTimeMS": 1}


def main(out_dir):
    def write(name, value, status=None):  # (a fixture is named after the address: one that ends in .json gives .json.json)
        with open(os.path.join(out_dir, name + ".json"), "w") as f:
            if value is not None:
                json.dump(value, f, ensure_ascii=False, separators=(",", ":"))
            f.write("\n" if value is not None else "")
        if status:
            with open(os.path.join(out_dir, name + ".status"), "w") as f:
                f.write("%d\n" % status)

    def write_list(name, stories, extra=()):
        ids = [s[0] for s in stories]
        ids[2:2] = extra
        write("GET_%s_v0_%s.json" % (ORDER, name), ids)
        asked = ids[:34]  # a few more than the thirty shown: the job advert among them is unknown to the search
        query = "tags=(story,poll),(" + ",".join("story_%d" % i for i in asked) + ")&hitsPerPage=%d" % len(asked) + STORY_ATTRS
        # The search answers in its own order (by points), and does not know the job advert.
        hits = sorted((story_hit(s) for s in stories if s[0] in asked), key=lambda h: -h["points"])
        write("GET_%s_api_v1_search_%s" % (SEARCH, fnv1a(query)), search(hits, len(asked)))

    def write_comments(story_id, comments, status=None):
        query = "tags=comment,story_%d&hitsPerPage=%d" % (story_id, COMMENTS_MAX) + COMMENT_ATTRS
        hits = sorted((comment_hit(c) for c in comments), key=lambda h: len(h["comment_text"] or ""))  # any order but the tree's
        write("GET_%s_api_v1_search_%s" % (SEARCH, fnv1a(query)), search(hits, COMMENTS_MAX) if status is None else None, status)

    write_list("topstories", TOP, extra=[JOB])
    write_list("newstories", NEW)
    write_list("askstories", ASK)
    write_list("showstories", SHOW)

    write("GET_%s_v0_item_%d.json" % (ORDER, S1), item(TOP[0], KIDS_1))
    write_comments(S1, COMMENTS_1)
    write("GET_%s_v0_item_%d.json" % (ORDER, S2), item(TOP[1], KIDS_2, ASK_TEXT))
    write_comments(S2, COMMENTS_2)
    write("GET_%s_v0_item_%d.json" % (ORDER, TOP[2][0]), item(TOP[2]))  # no comments: the search is not asked
    write("GET_%s_v0_item_%d.json" % (ORDER, S4), item(TOP[3], [38250401]))
    write_comments(S4, [], status=503)

    def write_article(url, text=None, error=None, status=None):
        body = json.dumps({"url": url}, separators=(",", ":"))  # what the app posts
        name = os.path.join(out_dir, "POST_%s__b%s" % (READER, fnv1a(body)))
        with open(name + ".json", "w") as f:
            f.write(text if text is not None else json.dumps(error, separators=(",", ":")) + "\n")
        if status:
            with open(name + ".status", "w") as f:
                f.write("%d\n" % status)

    def write_fallback(url, value, status=None):
        body = json.dumps({"url": url}, separators=(",", ":"))
        name = os.path.join(out_dir, "POST_%s__b%s" % (FALLBACK, fnv1a(body)))
        with open(name + ".json", "w") as f:
            f.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")
        if status:
            with open(name + ".status", "w") as f:
                f.write("%d\n" % status)

    write_article(TOP[0][2], ARTICLE_1)
    write_article(TOP[4][2], CAPTCHA_5)
    write_fallback(TOP[4][2], {"success": True, "url": TOP[4][2], "title": TOP[4][1], "content": ARTICLE_5,
                               "timestamp": "2026-01-15 12:30:00", "method": "Cloudflare Workers AI", "duration_ms": 0})
    # A page the service can not fetch: its error, as JSON with a sentence for people.
    write_article(TOP[3][2], error={"data": None, "path": "url", "code": 422, "name": "SubmittedDataMalformedError", "status": 42203,
                                    "message": "Domain 'blog.example.net' could not be resolved",
                                    "readableMessage": "SubmittedDataMalformedError: Domain 'blog.example.net' could not be resolved"}, status=422)
    # The second reader can not read it either: the first one's words are shown.
    write_fallback(TOP[3][2], {"success": False, "error": "Error from The API: All conversion methods failed. The page might be empty or inaccessible."},
                   status=500)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
