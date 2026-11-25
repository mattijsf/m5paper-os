// The pages of the docs site, in sidebar order. scripts/sync-docs.mjs turns each source file into a page with this
// title and description; astro.config.mjs builds the sidebar from the same list. Add a doc here to publish it.

export const BASE = "/m5paper-os";
export const REPO = "https://github.com/mattijsf/m5paper-os";

export const GROUPS = [
  {
    label: "Start",
    pages: [
      { src: "README.md", slug: "getting-started", title: "Getting started",
        description: "Build the simulator and the firmware, flash a PaperS3, and find your way around the code." },
      { slug: "screens", title: "Screens",
        description: "What PaperOS looks like on the panel: the launcher, the sleep screen and the apps." },
    ],
  },
  {
    label: "Project",
    pages: [
      { src: "docs/STATUS.md", slug: "status", title: "Status",
        description: "What works, the latest measurements, the checks still to run on the device, and known pitfalls." },
      { src: "docs/ROADMAP.md", slug: "roadmap", title: "Roadmap",
        description: "Phases and task cards: what is done, what is open, and how a task is worked." },
      { src: "docs/DECISIONS.md", slug: "decisions", title: "Decisions",
        description: "Every design decision, D1 to D41, with its reasons and the rules that follow from it." },
    ],
  },
  {
    label: "Design",
    pages: [
      { src: "docs/ARCHITECTURE.md", slug: "architecture", title: "Architecture",
        description: "Layers, threads, the main loop, the hardware abstraction, and the memory and flash budgets." },
      { src: "docs/HARDWARE.md", slug: "hardware", title: "Hardware",
        description: "The M5PaperS3 facts the software depends on: pins, power behaviour, display driver and toolchain." },
      { src: "docs/DISPLAY.md", slug: "display", title: "Display",
        description: "How PaperOS drives the e-ink panel: refresh modes, fast then settle, the ghost budget and tap latency." },
      { src: "docs/UI.md", slug: "ui", title: "UI system",
        description: "The Ink design system: tokens, widgets, gestures, fonts, icons and emoji for a 16-gray panel." },
      { src: "docs/POWER.md", slug: "power", title: "Power and Wi-Fi",
        description: "Power states, light sleep, the sleep screen, scheduled refresh and when Wi-Fi is on." },
      { src: "docs/SIMULATOR.md", slug: "simulator", title: "Simulator and tests",
        description: "The desktop simulator, UI scripts against golden screenshots, HTTP fixtures and the device test tools." },
      { src: "docs/APPS.md", slug: "writing-apps", title: "Writing an app",
        description: "How a PaperOS app is built: registration, lifecycle, screens, threads and error states." },
    ],
  },
  {
    label: "Backend",
    pages: [
      { src: "docs/BACKEND.md", slug: "home-assistant", title: "Home Assistant",
        description: "How the device uses Home Assistant: the connection, the API calls, the inbox relay and privacy." },
      { src: "docs/COMPANION.md", slug: "companion", title: "Companion protocol",
        description: "The end-to-end encrypted protocol between the Android companion app, Home Assistant and the device." },
      { src: "android/README.md", slug: "companion-app", title: "Companion app",
        description: "The Android companion app: what it reads, how it pairs, how to build it and how to check it." },
    ],
  },
  {
    label: "Apps",
    pages: [
      { src: "docs/apps/launcher.md", slug: "apps/launcher", title: "Launcher",
        description: "The home screen, the sleep screen with the day's board, the quick panel and what happens when idle." },
      { src: "docs/apps/settings.md", slug: "apps/settings", title: "Settings",
        description: "Settings pages, the config file, secrets on the SD card, phone pairing, files over Wi-Fi and wireless development." },
      { src: "docs/apps/reader.md", slug: "apps/reader", title: "Reader",
        description: "The e-book reader: EPUB, TXT and Markdown, the library, the page cache and articles to read later." },
      { src: "docs/apps/home.md", slug: "apps/home", title: "Home",
        description: "A Home Assistant remote: lights, climate, switches and music, set up with home.json." },
      { src: "docs/apps/inbox.md", slug: "apps/inbox", title: "WhatsApp and Mail",
        description: "Notifications from the phone, read on the panel, with replies sent through the companion app." },
      { src: "docs/apps/agenda.md", slug: "apps/agenda", title: "Agenda",
        description: "The phone's calendar for the next two weeks, sent to the device through the encrypted relay." },
      { src: "docs/apps/weather.md", slug: "apps/weather", title: "Weather",
        description: "Weather from Home Assistant: now, the next hours, ten days and a rain graph, for up to four places." },
      { src: "docs/apps/teletekst.md", slug: "apps/teletekst", title: "Teletekst",
        description: "NOS Teletekst page by page, drawn as the classic 40 x 24 grid in ink and paper." },
      { src: "docs/apps/hackernews.md", slug: "apps/hackernews", title: "Hacker News",
        description: "Hacker News lists, comment threads and linked articles as readable pages of text." },
      { src: "docs/apps/somdoku.md", slug: "apps/somdoku", title: "Somdoku",
        description: "A daily sum puzzle, ported from the web game so scores compare across devices." },
      { src: "docs/apps/wordgame.md", slug: "apps/wordgame", title: "Word game",
        description: "A daily five-letter word game in Dutch, one word a day, six tries." },
      { src: "docs/apps/chess.md", slug: "apps/chess", title: "Chess",
        description: "Chess against the Lichess computer or offline against the device itself." },
    ],
  },
];

// Golden screenshots of the UI tests (tests/ui/golden/, 540x960, what the simulator draws) shown on the site.
export const SCREENS = [
  { file: "launcher-summary.png", caption: "Launcher: twelve apps, badges, what you were reading" },
  { file: "today-board.png", caption: "Sleep screen: the day's weather and agenda, left on the panel while off" },
  { file: "reader-p1.png", caption: "Reader: a book page" },
  { file: "home-lights.png", caption: "Home: lights and scenes from Home Assistant" },
  { file: "home-music.png", caption: "Home: the music tab" },
  { file: "weather.png", caption: "Weather: now and the next hours" },
  { file: "agenda-day.png", caption: "Agenda: the phone's calendar" },
  { file: "inbox-list.png", caption: "WhatsApp: notifications from the phone" },
  { file: "teletekst-news.png", caption: "Teletekst" },
  { file: "hackernews-article.png", caption: "Hacker News: a linked article as text" },
  { file: "wordgame-won.png", caption: "Word game" },
  { file: "chess-board.png", caption: "Chess" },
];
