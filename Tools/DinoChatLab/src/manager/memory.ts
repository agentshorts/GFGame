/**
 * Summary helpers. If src/core/index.ts exports fallbackSummarize and clampSummary, those win.
 * The memory branch (#75) should re-export them from that file.
 */

const CHARS_PER_TOKEN = 4;

export interface SummaryFns {
  fallbackSummarize(prev: string, lines: string[], capTokens: number): string;
  clampSummary(text: string, capTokens: number): string;
}

export function clampSummary(text: string, tokenCap: number): string {
  const maxChars = Math.max(0, Math.floor(tokenCap)) * CHARS_PER_TOKEN;
  const flat = text.replace(/\s+/g, " ").trim();
  if (flat.length <= maxChars) return flat;
  return flat.slice(flat.length - maxChars).trimStart();
}

/** Keeps the newest text and trims from the front. Same input, same output. */
export function fallbackSummarize(prev: string, lines: string[], capTokens: number): string {
  const addition = lines.map((line) => line.trim()).filter((line) => line.length > 0).join(" ");
  const merged = [prev.trim(), addition].filter((part) => part.length > 0).join(" ");
  return clampSummary(merged, capTokens);
}

export const localSummaryFns: SummaryFns = { fallbackSummarize, clampSummary };

interface CoreMemoryModule {
  fallbackSummarize?: (prev: string, turns: unknown, cap: number) => string;
  clampSummary?: (text: string, tokenCap: number) => string;
}

const coreLoaders = import.meta.glob("../core/index.ts") as Record<string, () => Promise<CoreMemoryModule>>;

export async function loadSummaryFns(): Promise<{ fns: SummaryFns; source: "core" | "shim" }> {
  const load = Object.values(coreLoaders)[0];
  if (!load) return { fns: localSummaryFns, source: "shim" };
  try {
    const mod = await load();
    if (typeof mod.fallbackSummarize !== "function" || typeof mod.clampSummary !== "function") {
      return { fns: localSummaryFns, source: "shim" };
    }
    const coreClamp = mod.clampSummary;
    const coreSummarize = mod.fallbackSummarize;
    return {
      source: "core",
      fns: {
        clampSummary(text, cap) {
          const out = coreClamp(text, cap);
          return typeof out === "string" ? out : clampSummary(text, cap);
        },
        fallbackSummarize(prev, lines, cap) {
          const turns = lines.map((text) => ({ text, speaker: "", role: "user" as const }));
          try {
            const out = coreSummarize(prev, turns, cap);
            if (typeof out === "string") return coreClamp(out, cap);
          } catch {
            const out = coreSummarize(prev, lines, cap);
            if (typeof out === "string") return out;
          }
          return fallbackSummarize(prev, lines, cap);
        },
      },
    };
  } catch {
    return { fns: localSummaryFns, source: "shim" };
  }
}

export const DINO_SUMMARY_TOKENS = 250;
export const RELATION_SUMMARY_TOKENS = 150;
