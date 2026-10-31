/**
 * Regenerates Tools/DinoChatLab/public/help/*.png for the #/help page.
 * Starts Vite, drives the lab with Playwright, and writes every file in
 * src/guide/helpShots.json.
 */
import { spawn } from "node:child_process";
import { mkdir, readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const { chromium } = require("playwright");

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = resolve(root, "public/help");
const port = Number(process.env.DINO_HELP_PORT ?? 4179);
const base = `http://127.0.0.1:${port}`;

const shots = JSON.parse(await readFile(resolve(root, "src/guide/helpShots.json"), "utf8"));
const expected = new Set(shots.map((shot) => shot.file));
const written = new Set();

function dismissedStore(openId) {
  const store = {};
  for (const id of ["crew", "events", "traces"]) {
    store[id] = { dismissed: id !== openId, step: 0 };
  }
  return JSON.stringify(store);
}

function labReadyScript() {
  const store = dismissedStore(null);
  return `(() => {
    localStorage.setItem("dcl:username", "Pip");
    localStorage.setItem("dcl:trace", "1");
    localStorage.setItem("dcl:wizards", ${JSON.stringify(store)});
    localStorage.removeItem("dcl:panel-info");
  })();`;
}

async function waitForServer(child) {
  const deadline = Date.now() + 40_000;
  let log = "";
  child.stdout?.on("data", (chunk) => {
    log += chunk.toString();
  });
  child.stderr?.on("data", (chunk) => {
    log += chunk.toString();
  });
  while (Date.now() < deadline) {
    if (child.exitCode != null) {
      throw new Error(`Vite exited early.\n${log}`);
    }
    try {
      const response = await fetch(`${base}/`);
      if (response.ok) return;
    } catch {
      /* still booting */
    }
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 250));
  }
  throw new Error(`Vite did not answer on ${base}.\n${log}`);
}

async function save(page, file, options = {}) {
  if (!expected.has(file)) throw new Error(`Unexpected shot ${file}`);
  await page.screenshot({ path: resolve(outDir, file), animations: "disabled", ...options });
  written.add(file);
  console.log(`wrote ${file}`);
}

async function saveLocator(locator, file) {
  if (!expected.has(file)) throw new Error(`Unexpected shot ${file}`);
  await locator.screenshot({ path: resolve(outDir, file), animations: "disabled" });
  written.add(file);
  console.log(`wrote ${file}`);
}

async function showHash(page, hash) {
  await page.evaluate((next) => {
    if (location.hash === next) location.hash = "#/help";
    location.hash = next;
  }, hash);
}

async function openWizard(page, id, hash) {
  await page.evaluate(({ id: openId, hash: next, store }) => {
    localStorage.setItem("dcl:wizards", store);
    if (location.hash === next) location.hash = "#/help";
    location.hash = next;
  }, { id, hash, store: dismissedStore(id) });
  await page.locator(`[data-wizard="${id}"]`).waitFor({ timeout: 20_000 });
}

const viteBin = resolve(root, "node_modules/vite/bin/vite.js");
const child = spawn(process.execPath, [viteBin, "--host", "127.0.0.1", "--port", String(port), "--strictPort"], {
  cwd: root,
  stdio: ["ignore", "pipe", "pipe"],
});

let browser;
try {
  await waitForServer(child);
  browser = await chromium.launch({
    headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage"],
  });
  await mkdir(outDir, { recursive: true });

  const context = await browser.newContext({
    viewport: { width: 1440, height: 1000 },
    deviceScaleFactor: 1,
  });
  await context.addInitScript(labReadyScript());
  const page = await context.newPage();

  await page.goto(`${base}/#/`, { waitUntil: "networkidle" });
  await page.getByRole("heading", { name: "Dino Chat Lab" }).waitFor();
  await page.getByRole("button", { name: "why you running", exact: true }).click();
  await page.getByRole("button", { name: "Send", exact: true }).click();
  await page.getByRole("button", { name: "Send", exact: true }).waitFor({ timeout: 90_000 });
  await page.locator("[aria-label='Transcript'] .bubble").first().waitFor();
  await page.addStyleTag({
    content: `
      html.shot-full .columns { align-items: start !important; }
      html.shot-full .bench,
      html.shot-full .debug,
      html.shot-full .log,
      html.shot-full .editor {
        max-height: none !important;
        height: auto !important;
        overflow: visible !important;
        min-height: 0 !important;
      }
      html.shot-full .sheet { flex: none !important; max-height: none !important; }
    `,
  });

  const withFull = async (fn) => {
    await page.evaluate(() => document.documentElement.classList.add("shot-full"));
    try {
      await fn();
    } finally {
      await page.evaluate(() => document.documentElement.classList.remove("shot-full"));
    }
  };

  await save(page, "chat-lab.png");
  await withFull(async () => {
    await saveLocator(page.locator("[aria-label='Chat controls']"), "panel-talk.png");
    await page.locator("[data-panel-info='talk'] summary").click();
    await saveLocator(page.locator("[data-panel-info='talk']"), "panel-info-talk.png");
    await saveLocator(page.locator("[aria-label='Transcript']"), "panel-transcript.png");
    await saveLocator(page.locator("[aria-label='Alignment debug']"), "panel-decision.png");
  });

  const book = page.locator("[aria-label='Dinobook']");
  await book.scrollIntoViewIfNeeded();
  await page.getByRole("button", { name: "Seed lab crew", exact: true }).click();
  await page.getByRole("button", { name: "Player joined", exact: true }).click();
  await page.locator("[aria-label='Dinobook'] [role='feed'] .bubble").first().waitFor({ timeout: 120_000 });
  await saveLocator(book, "panel-dinobook.png");

  await showHash(page, "#/trace");
  await page.getByRole("heading", { name: "What each engine said" }).waitFor();
  await page.locator("[aria-label='Trace log'] .bubble").first().waitFor({ timeout: 20_000 });
  await save(page, "trace-log.png", { fullPage: true });

  await showHash(page, "#/personas");
  await page.locator("[data-pane='editor']").waitFor();
  await page.locator("[data-pane='chat'] textarea").fill("hello");
  await page.locator("[data-pane='chat'] button[type='submit']").click();
  await page.locator("[data-pane='chat'] .bubble").first().waitFor({ timeout: 30_000 });
  await save(page, "personas.png");
  await withFull(async () => {
    await saveLocator(page.locator("[data-pane='list']"), "panel-dino-list.png");
    await saveLocator(page.locator("[data-pane='editor']"), "panel-editor.png");
    await saveLocator(page.locator("[data-pane='chat']"), "panel-test-chat.png");
  });

  await openWizard(page, "crew", "#/personas");
  await saveLocator(page.locator("[data-wizard='crew']"), "wizard-crew.png");
  await openWizard(page, "events", "#/");
  await page.locator("[data-wizard='events']").scrollIntoViewIfNeeded();
  await saveLocator(page.locator("[data-wizard='events']"), "wizard-events.png");
  await openWizard(page, "traces", "#/trace");
  await saveLocator(page.locator("[data-wizard='traces']"), "wizard-traces.png");

  await context.close();

  const gateContext = await browser.newContext({
    viewport: { width: 1440, height: 1000 },
    deviceScaleFactor: 1,
  });
  await gateContext.addInitScript(() => {
    localStorage.removeItem("dcl:username");
  });
  const gate = await gateContext.newPage();
  await gate.goto(`${base}/#/personas`, { waitUntil: "networkidle" });
  await gate.getByRole("heading", { name: "Pick a username" }).waitFor();
  await save(gate, "username-gate.png");
  await gateContext.close();

  const missing = [...expected].filter((file) => !written.has(file));
  if (missing.length) throw new Error(`Missing screenshots: ${missing.join(", ")}`);
} finally {
  await browser?.close().catch(() => {});
  if (child.exitCode == null && child.pid) child.kill("SIGTERM");
}
