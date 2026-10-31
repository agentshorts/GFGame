import type { TraceDecision } from "./traceTypes";
import type { Cue } from "./types";
import type { ChatMode, Intent } from "./types";
import { INTENTS } from "./types";
import { contextBlob, estimateTokens, type ContextState } from "./context";
import { engineUtterance } from "./session";
import type { PromptMessage } from "./prompt";
import { speechHint, type Decision, type DecisionInput } from "./alignment";

export const INTENT_KEYWORD: Record<Intent, string | null> = {
  attack: "attack",
  ambush: "ambush",
  barter: "barter",
  beg: "beg",
  protect: "protect",
  extort: "extort",
  ignore: null,
};

export interface Steer {
  source: "intent" | "chat";
  keyword: string | null;
  utterance: string;
  reason: string;
}

export interface ModelSteer {
  intent: Intent;
  summary?: string;
  rephrase: boolean;
  usedModel: boolean;
}

export function describeWorld(input: DecisionInput, intent: Intent): string {
  const carried = input.inventory.filter((stack) => stack.count > 0);
  const items = carried.length > 0 ? carried.map((stack) => `${stack.name} x${stack.count}`).join(", ") : "empty";
  const book = input.dinobook ? "yes" : "no";
  return `Distance ${input.distanceFt} ft, smell ${input.smellRangeFt} ft, odds ${input.winOdds.toFixed(2)}, dinobook ${book}, carrying ${items}. Intent ${intent}.`;
}

export function matchCue(cues: Cue[], text: string): string | null {
  const tokens = new Set(
    text.toLowerCase().replace(/[^a-z0-9\s]/g, " ").split(/\s+/).filter((token) => token.length > 0),
  );
  for (const cue of cues) {
    for (const keyword of cue.keywords) {
      if (tokens.has(keyword)) return keyword;
    }
  }
  return null;
}

export function pickSteer(args: {
  mode: ChatMode;
  speaker: string;
  userText: string;
  intent: Intent;
  cues: Cue[];
}): Steer {
  if (args.intent !== "ignore") {
    const keyword = INTENT_KEYWORD[args.intent];
    return {
      source: "intent",
      keyword,
      utterance: keyword ?? args.userText,
      reason: `World intent ${args.intent} steers the classic engine.`,
    };
  }
  const keyword = matchCue(args.cues, args.userText);
  return {
    source: "chat",
    keyword,
    utterance: engineUtterance(args.mode, args.speaker, args.userText),
    reason: keyword
      ? `No world action. Chat keyword ${keyword}.`
      : "No world action and no cue. The classic engine can fall back.",
  };
}

export function steerDecision(steer: Steer): TraceDecision {
  return {
    stage: "steer",
    chosen: steer.utterance,
    reason: steer.reason,
    detail: { source: steer.source, keyword: steer.keyword },
  };
}

/**
 * When the context model picks a different intent, the shown reasons and speech hint
 * follow that intent. The world facts stay. An unchanged intent keeps the alignment lines.
 */
export function reconcileSteeredDecision(
  decision: Decision,
  intent: Intent,
  note: string,
  itemName = "",
): Decision {
  if (intent === decision.intent) return decision;
  const item = itemName.trim() || decision.smelledWant[0] || "";
  const because = note.trim().length > 0 ? note.trim() : `Context model chose ${intent}.`;
  return {
    ...decision,
    intent,
    reasons: [...decision.reasons.slice(0, -1), because],
    speechHint: speechHint(intent, item),
  };
}

export function parseModelSteer(text: string, fallback: Intent): ModelSteer {
  const match = text.match(/\{[\s\S]*\}/);
  if (!match) return { intent: fallback, rephrase: false, usedModel: false };
  try {
    const data = JSON.parse(match[0]) as { intent?: unknown; summary?: unknown; rephrase?: unknown };
    const intent = typeof data.intent === "string" && (INTENTS as readonly string[]).includes(data.intent)
      ? data.intent as Intent
      : fallback;
    return {
      intent,
      summary: typeof data.summary === "string" ? data.summary.trim() : undefined,
      rephrase: data.rephrase === true,
      usedModel: true,
    };
  } catch {
    return { intent: fallback, rephrase: false, usedModel: false };
  }
}

export function fitToBudget(text: string, budget: number): string {
  let next = text.trim();
  const cap = Math.max(1, Math.floor(budget));
  while (next.length > 0 && estimateTokens(next) > cap) {
    const cut = Math.max(1, Math.floor(next.length * 0.8));
    next = next.slice(0, cut).trimEnd();
  }
  return next;
}

function tighten(messages: PromptMessage[], budget: number): PromptMessage[] {
  const next = messages.map((message) => ({ ...message }));
  while (messageTokenEstimate(next) > budget) {
    const target = next[next.length - 1];
    if (!target || target.content.length === 0) break;
    target.content = target.content.slice(0, Math.max(0, target.content.length - 4)).trimEnd();
  }
  return next;
}

export function buildSteerMessages(state: ContextState, decision: Decision, personaName: string, budget: number): PromptMessage[] {
  const allowed = INTENTS.join(", ");
  const system = fitToBudget([
    "You are the memory layer in front of a classic chatbot.",
    `The dino is ${personaName}.`,
    "Reply with JSON only: {\"summary\":\"...\",\"intent\":\"beg\",\"rephrase\":false}.",
    `intent must be one of: ${allowed}.`,
    `The alignment module recommends ${decision.intent}. Change it only when the memory makes that recommendation wrong.`,
    "Do not invent a backstory. Keep the summary short.",
  ].join(" "), Math.max(1, budget - 1));
  const user = fitToBudget(
    [`Recommended intent: ${decision.intent}.`, decision.reasons.join(" "), contextBlob(state)].join("\n"),
    Math.max(1, budget - estimateTokens(system)),
  );
  return tighten([
    { role: "system", content: system },
    { role: "user", content: user },
  ], budget);
}

export function buildRephraseMessages(args: {
  systemPrompt: string;
  classicLine: string;
  memory: string;
  budget: number;
}): PromptMessage[] {
  const cap = Math.max(1, args.budget);
  const system = fitToBudget(args.systemPrompt, Math.max(1, Math.floor(cap / 2)));
  const user = fitToBudget(
    [
      args.memory ? `Memory:\n${args.memory}` : "",
      `Classic line: ${args.classicLine}`,
      "Rephrase that line in character. Output only the line. Do not add biography. Keep it PG.",
    ].filter((part) => part.length > 0).join("\n\n"),
    Math.max(1, cap - estimateTokens(system)),
  );
  return tighten([
    { role: "system", content: system },
    { role: "user", content: user },
  ], cap);
}

export function messageTokenEstimate(messages: PromptMessage[]): number {
  return messages.reduce((sum, message) => sum + estimateTokens(message.content), 0);
}
