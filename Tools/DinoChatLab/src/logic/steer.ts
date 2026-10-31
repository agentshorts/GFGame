import type { TraceDecision } from "./traceTypes";
import type { Cue } from "./types";
import type { ChatMode, Intent } from "./types";
import { engineUtterance } from "./session";
import type { DecisionInput } from "./alignment";

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
