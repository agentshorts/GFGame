import { estimateTokens } from "../logic/context";
import { filterOutput } from "../logic/filter";
import { POST_TOKEN_CAP } from "./types";

export function normalizeLine(text: string): string {
  return text.toLowerCase().replace(/[^a-z0-9]+/g, " ").trim();
}

export function clampToTokens(text: string, cap = POST_TOKEN_CAP): string {
  let out = text.replace(/\s+/g, " ").trim();
  if (cap <= 0 || !out) return "";
  if (estimateTokens(out) <= cap) return out;
  const words = out.split(" ");
  while (words.length > 1 && estimateTokens(words.join(" ")) > cap) words.pop();
  let joined = words.join(" ");
  while (joined.length > 0 && estimateTokens(joined) > cap) {
    joined = joined.slice(0, -1).trimEnd();
  }
  return joined.trim();
}

/** Later speakers name someone who already talked. The name is kept inside the token cap. */
export function withReference(text: string, priorSpeaker: string | null, cap = POST_TOKEN_CAP): string {
  const body = text.replace(/\s+/g, " ").trim();
  if (!priorSpeaker) return clampToTokens(body, cap);
  if (body.toLowerCase().includes(priorSpeaker.toLowerCase())) return clampToTokens(body, cap);
  const suffix = ` Heard ${priorSpeaker}.`;
  const room = Math.max(1, cap - estimateTokens(suffix));
  return clampToTokens(`${clampToTokens(body, room)}${suffix}`, cap);
}

export interface AcceptedSpeech {
  text: string;
  tokens: number;
  filtered: boolean;
}

/**
 * Drop a line that still matches one already in the window.
 * A reference to the previous speaker is the only rewrite, and it has to be a real change.
 */
export function acceptSpeech(
  draft: string,
  priorTexts: readonly string[],
  priorSpeaker: string | null,
  cap = POST_TOKEN_CAP,
): AcceptedSpeech | null {
  const seen = new Set(priorTexts.map((line) => normalizeLine(line)).filter((line) => line.length > 0));
  let text = clampToTokens(draft, cap);
  if (!text) return null;
  if (seen.has(normalizeLine(text))) {
    if (!priorSpeaker) return null;
    const varied = withReference(text, priorSpeaker, cap);
    if (!varied || seen.has(normalizeLine(varied)) || normalizeLine(varied) === normalizeLine(text)) {
      return null;
    }
    text = varied;
  }
  const filtered = filterOutput(text);
  const cleaned = clampToTokens(filtered.text, cap);
  if (!cleaned || seen.has(normalizeLine(cleaned))) return null;
  return { text: cleaned, tokens: estimateTokens(cleaned), filtered: filtered.filtered };
}
