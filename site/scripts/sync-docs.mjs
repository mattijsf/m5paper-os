// Builds the site's pages from the Markdown in the repository, so that Markdown stays the one source and still reads
// well on GitHub. Writes (not committed):
//   src/content/docs/        one page per entry of pages.mjs, plus index.mdx (landing.mdx) and screens.md
//   public/screens/          the golden screenshots listed in pages.mjs SCREENS
// A page gets Starlight frontmatter (title and description from pages.mjs, last change from git, an edit link to its
// source); the source's H1 is dropped because Starlight draws the title. Links to other published docs become relative
// site links, links to any other file in the repository become GitHub links.
import { execFileSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, posix, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { BASE, GROUPS, REPO, SCREENS } from "../pages.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const site = resolve(here, "..");
const root = resolve(site, "..");
const out = join(site, "src/content/docs");
const screensOut = join(site, "public/screens");

const pages = GROUPS.flatMap((g) => g.pages);
const bySource = new Map(pages.filter((p) => p.src).map((p) => [p.src, p]));

// every Markdown doc in the repository should be on the site, so a new doc is not forgotten
for (const dir of ["docs", "docs/apps"]) {
  for (const f of execFileSync("git", ["ls-files", `${dir}/*.md`], { cwd: root, encoding: "utf8" }).split("\n")) {
    if (f && posix.dirname(f) === dir && !bySource.has(f)) throw new Error(`${f} is not in site/pages.mjs`);
  }
}

// only outside inline code: `[text](url)` in a code span is an example, not a link
function rewriteLinks(from, line) {
  return line
    .split(/(`[^`]*`)/)
    .map((part, i) => (i % 2 ? part : rewritePart(from, part)))
    .join("");
}

function rewritePart(from, text) {
  return text.replace(/\]\(([^)\s]+)\)/g, (all, target) => {
    if (/^(https?:|mailto:|#|\/)/.test(target)) return all;
    const [path, anchor] = target.split("#");
    const resolved = posix.normalize(posix.join(posix.dirname(from.src), path));
    const hash = anchor ? `#${anchor}` : "";
    const to = bySource.get(resolved);
    if (to) return `](${posix.relative(from.slug, to.slug) || "."}/${hash})`;
    const abs = join(root, resolved);
    if (!existsSync(abs)) throw new Error(`${from.src}: link to ${target}, which does not exist`);
    return `](${REPO}/${statSync(abs).isDirectory() ? "tree" : "blob"}/main/${resolved}${hash})`;
  });
}

function lastChange(src) {
  try {
    return execFileSync("git", ["log", "-1", "--format=%cI", "--", src], { cwd: root, encoding: "utf8" }).trim();
  } catch {
    return "";
  }
}

function frontmatter(page, extra = []) {
  return ["---", `title: ${JSON.stringify(page.title)}`, `description: ${JSON.stringify(page.description)}`, ...extra, "---", ""].join("\n");
}

rmSync(out, { recursive: true, force: true });
rmSync(screensOut, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
mkdirSync(screensOut, { recursive: true });

for (const page of pages.filter((p) => p.src)) {
  const body = [];
  let h1 = false;
  let fence = null;
  for (const line of readFileSync(join(root, page.src), "utf8").split("\n")) {
    const f = line.match(/^(`{3,}|~{3,})/);
    if (f) {
      fence = fence ? (line.startsWith(fence) ? null : fence) : f[1];
      body.push(line);
    } else if (fence) {
      body.push(line);
    } else if (!h1 && /^# /.test(line)) {
      h1 = true;
    } else {
      body.push(rewriteLinks(page, line));
    }
  }
  if (!h1) throw new Error(`${page.src}: no H1`);
  const date = lastChange(page.src);
  const extra = [`editUrl: ${REPO}/edit/main/${page.src}`, ...(date ? [`lastUpdated: ${date}`] : [])];
  const file = join(out, `${page.slug}.md`);
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, frontmatter(page, extra) + body.join("\n").replace(/^\n+/, ""));
}

// the screenshots, and the page that shows them
for (const s of SCREENS) copyFileSync(join(root, "tests/ui/golden", s.file), join(screensOut, s.file));
const screens = pages.find((p) => p.slug === "screens");
const figures = SCREENS.map(
  (s) => `  <figure><img src="${BASE}/screens/${s.file}" alt="${s.caption}" width="540" height="960" loading="lazy" />` +
    `<figcaption>${s.caption}</figcaption></figure>`,
).join("\n");
writeFileSync(
  join(out, "screens.md"),
  frontmatter(screens) +
    "These are golden screenshots of the UI tests: 540 x 960 pixels in 16 grays, drawn by the simulator from test data. " +
    "The device's own frame dumps match them pixel for pixel. On the panel itself white is a light gray and there is no " +
    "backlight; see [Display](../display/) for how a screen gets there.\n\n" +
    `<div class="screens not-content">\n${figures}\n</div>\n`,
);

// the landing page is written for the site (site/landing.mdx)
writeFileSync(join(out, "index.mdx"), readFileSync(join(site, "landing.mdx"), "utf8"));

console.log(`sync-docs: ${pages.length + 1} pages, ${SCREENS.length} screenshots`);
