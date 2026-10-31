/**
 * Packs one dino's window for the notebook.
 * There is no shared model. The classic engine, including creature talk, speaks.
 */

import { assembleTurn, messageTokenEstimate, trimPromptToBudget } from "../core/memory";
import type { ContextTurn, PromptMessage } from "../core/types";
import { DEFAULT_CONTEXT_BUDGET } from "../core/types";
import { createContext, remember, setBudget, type ContextState } from "./context";

/** Shared window. Same 4096 estimate the rest of the lab uses. */
export const BRAIN_CONTEXT_CAP = DEFAULT_CONTEXT_BUDGET;

export const BRAIN_REASONS = {
  classicFailed: "The classic engine failed. The fallback line stands.",
  empty: "The classic engine returned an empty line. The fallback stands.",
} as const;

export type BrainClassicSource = "creature" | "eliza" | "aiml" | "rivescript" | "megahal" | "fallback";

export interface BrainTurn {
  dinoId: string;
  personaName: string;
  personaBlock: string;
  worldNote: string;
  dinoSummary: string;
  relationshipSummary: string;
  subjectName: string;
  elder: boolean;
  playerRecord: string;
  recentTurns: readonly ContextTurn[];
  /** Clamped to the 4096 cap. Larger knobs still pack a 4k prompt. */
  budget?: number;
  replyReserve?: number;
  temperature: number;
  maxTokens: number;
  utterance: string;
}

export interface BrainClassicReply {
  text: string;
  source: BrainClassicSource;
}

export interface BrainResult {
  status: "classic" | "fallback";
  text: string;
  source: BrainClassicSource;
  fallbackReason?: string;
  attempted: boolean;
  prompt: PromptMessage[];
  ms: number;
}

export interface BrainPoolOptions {
  classic: (turn: BrainTurn) => Promise<BrainClassicReply>;
}

export interface BrainPool {
  ask(turn: BrainTurn): Promise<BrainResult>;
  cancel(dinoId: string): void;
  unload(): void;
  depth(): number;
}

function brainBudget(value: number | undefined): number {
  return Math.min(createContext(value ?? BRAIN_CONTEXT_CAP).budget, BRAIN_CONTEXT_CAP);
}

function replyReserve(turn: BrainTurn): number {
  const raw = turn.replyReserve ?? turn.maxTokens;
  if (!Number.isFinite(raw)) return 80;
  return Math.max(0, Math.floor(raw));
}

function roundMs(ms: number): number {
  return Math.round(ms * 10) / 10;
}

/**
 * Pack one dino's window with `context.ts`, then the prompt with `memory.ts`.
 * The caller passes that dino's notes only. This function does not read anyone else.
 */
export function packDinoPrompt(turn: BrainTurn): { state: ContextState; prompt: PromptMessage[] } {
  const budget = brainBudget(turn.budget);
  let state = setBudget(
    {
      budget,
      summary: turn.dinoSummary,
      worldNote: turn.worldNote,
      turns: [],
    },
    budget,
  );
  for (const item of turn.recentTurns) {
    state = remember(state, { speaker: item.speaker, text: item.text }, turn.worldNote);
  }
  state = setBudget(state, budget);
  const offset = Math.max(0, turn.recentTurns.length - state.turns.length);
  const recentTurns: ContextTurn[] = state.turns.map((item, index) => {
    const source = turn.recentTurns[offset + index];
    return { speaker: item.speaker, text: item.text, role: source?.role };
  });
  let prompt = assembleTurn({
    personaName: turn.personaName,
    personaBlock: turn.personaBlock,
    worldNote: state.worldNote,
    dinoSummary: state.summary,
    relationshipSummary: turn.relationshipSummary,
    subjectName: turn.subjectName,
    elder: turn.elder,
    playerRecord: turn.elder ? turn.playerRecord : "",
    recentTurns,
    budget,
    replyReserve: replyReserve(turn),
  });
  if (messageTokenEstimate(prompt) > BRAIN_CONTEXT_CAP) {
    prompt = trimPromptToBudget(prompt, BRAIN_CONTEXT_CAP, 0);
  }
  return {
    state,
    prompt: prompt.map((message) => ({ role: message.role, content: message.content })),
  };
}

export function createBrainPool(options: BrainPoolOptions): BrainPool {
  const classic = options.classic;
  return {
    async ask(turn) {
      const packed = packDinoPrompt(turn);
      const started = performance.now();
      try {
        const reply = await classic(turn);
        const text = reply.text.trim();
        if (text.length === 0) {
          return {
            status: "fallback",
            text: "...",
            source: "fallback",
            fallbackReason: BRAIN_REASONS.empty,
            attempted: false,
            prompt: packed.prompt,
            ms: roundMs(performance.now() - started),
          };
        }
        return {
          status: "classic",
          text,
          source: reply.source,
          attempted: false,
          prompt: packed.prompt,
          ms: roundMs(performance.now() - started),
        };
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        return {
          status: "fallback",
          text: "...",
          source: "fallback",
          fallbackReason: `${BRAIN_REASONS.classicFailed} (${message}).`,
          attempted: false,
          prompt: packed.prompt,
          ms: roundMs(performance.now() - started),
        };
      }
    },
    cancel() {},
    unload() {},
    depth() {
      return 0;
    },
  };
}
