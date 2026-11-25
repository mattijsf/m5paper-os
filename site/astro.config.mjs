import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import mermaid from "astro-mermaid";
import { BASE, GROUPS, REPO } from "./pages.mjs";

// The docs site of PaperOS: Starlight, as for walkit/docs and mattix.io. The pages are not written here:
// scripts/sync-docs.mjs builds them from the Markdown in the repository (pages.mjs lists which, in sidebar order).
export default defineConfig({
  site: "https://mattijsf.github.io",
  base: BASE,
  integrations: [
    // before starlight: ```mermaid blocks become diagrams in the browser, in the light or dark theme
    mermaid({ autoTheme: true }),
    starlight({
      title: "PaperOS",
      description: "Custom firmware for the M5Stack M5PaperS3 e-ink device, with a desktop simulator.",
      favicon: "/favicon.svg",
      social: [{ icon: "github", label: "GitHub", href: REPO }],
      customCss: ["./src/styles/custom.css"],
      lastUpdated: true,
      head: [{ tag: "meta", attrs: { name: "theme-color", content: "#111111" } }],
      sidebar: GROUPS.map((g) => ({ label: g.label, items: g.pages.map((p) => p.slug) })),
    }),
  ],
});
