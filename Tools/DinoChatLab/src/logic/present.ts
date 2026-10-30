import { fillTarget } from "./fillTarget";
import { filterOutput } from "./filter";
import { GAME_CHAT_MAX_CHARS, type Replacement } from "./types";

export function clampChatText(text: string, max = GAME_CHAT_MAX_CHARS): string {
  return text.trim().slice(0, max);
}

/** Rule-based engines have no tokenizer. Four characters is a rough English stand-in for one token. */
export function replyCharBudget(maxReplyLength: number, maxTokens: number): number {
  const tokens = Number.isFinite(maxTokens) ? Math.floor(maxTokens) : 1;
  const fromTokens = Math.max(1, tokens) * 4;
  return Math.max(1, Math.min(maxReplyLength, fromTokens));
}

export function clipText(text: string, maxChars: number): string {
  const flat = text.replace(/\s+/g, " ").trim();
  if (flat.length <= maxChars) return flat;
  if (maxChars <= 1) return flat.slice(0, maxChars);
  const room = maxChars - 1;
  const cut = flat.slice(0, room);
  const lastSpace = cut.lastIndexOf(" ");
  const body = lastSpace > room * 0.6 ? cut.slice(0, lastSpace) : cut;
  return `${body.trimEnd()}…`;
}

export interface PresentedReply {
  text: string;
  rawText: string;
  filtered: boolean;
  replacements: Replacement[];
}

export function presentReply(engineText: string, options: {
  playerName: string;
  maxReplyLength: number;
  maxTokens: number;
}): PresentedReply {
  const filled = fillTarget(engineText.trim(), options.playerName);
  const filtered = filterOutput(filled);
  const budget = replyCharBudget(options.maxReplyLength, options.maxTokens);
  return {
    text: clipText(filtered.text, budget),
    rawText: filled,
    filtered: filtered.filtered,
    replacements: filtered.replacements,
  };
}
