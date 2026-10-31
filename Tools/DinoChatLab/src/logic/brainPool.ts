/**
 * One WebLLM brain for every dino.
 * A single generation runs at a time. Each dino holds one queue slot, in the
 * order the slots were opened. A newer ask for that dino replaces the slot.
 * The prompt is packed from that ask alone. Nothing is kept for the next dino.
 */

import { assembleTurn, messageTokenEstimate, trimPromptToBudget } from "../core/memory";
import type { ContextTurn, PromptMessage } from "../core/types";
import { DEFAULT_CONTEXT_BUDGET } from "../core/types";
import { createContext, remember, setBudget, type ContextState } from "./context";

/** Shared brain window. Same 4096 estimate the rest of the lab uses. */
export const BRAIN_CONTEXT_CAP = DEFAULT_CONTEXT_BUDGET;

export const DEFAULT_BRAIN_TIMEOUT_MS = 20_000;

export const BRAIN_REASONS = {
  notLoaded: "No WebLLM model is loaded. The classic engine speaks.",
  timeout: "WebLLM timed out waiting for the shared brain. The classic engine speaks.",
  unload: "The shared brain unloaded this turn. The classic engine speaks.",
  empty: "WebLLM returned an empty line. The classic engine speaks.",
  superseded: "A newer line for this dino took the shared brain.",
  cancelled: "This dino's turn on the shared brain was cancelled.",
} as const;

export type BrainClassicSource = "eliza" | "aiml" | "rivescript" | "megahal" | "fallback";

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
  /** Clamped to the 4096 cap. Larger knobs still pack a 4k brain prompt. */
  budget?: number;
  replyReserve?: number;
  temperature: number;
  maxTokens: number;
  utterance: string;
}

export interface BrainCompletion {
  text: string;
  promptTokens?: number;
  completionTokens?: number;
  tokensPerSec?: number;
  prefillTokensPerSec?: number;
  runtimeStatsText?: string;
}

/** Injectable. Tests pass a fake. The lab passes the browser engine. */
export interface BrainEngine {
  readonly loadedModelId: string | null;
  complete(args: {
    messages: PromptMessage[];
    temperature: number;
    maxTokens: number;
    signal: AbortSignal;
  }): Promise<BrainCompletion>;
}

export interface BrainClassicReply {
  text: string;
  source: BrainClassicSource;
}

export interface BrainResult {
  status: "model" | "fallback" | "dropped";
  text: string;
  source: "webllm" | BrainClassicSource | "none";
  fallbackReason?: string;
  /** True when this turn waited on the shared engine. False when no model is loaded. */
  attempted: boolean;
  prompt: PromptMessage[];
  ms: number;
  promptTokens?: number;
  completionTokens?: number;
  tokensPerSec?: number;
  prefillTokensPerSec?: number;
  runtimeStatsText?: string;
}

export interface BrainAskOptions {
  timeoutMs?: number;
  signal?: AbortSignal;
}

export interface BrainPoolOptions {
  engine: BrainEngine;
  classic: (turn: BrainTurn) => Promise<BrainClassicReply>;
  timeoutMs?: number;
}

export interface BrainPool {
  ask(turn: BrainTurn, options?: BrainAskOptions): Promise<BrainResult>;
  cancel(dinoId: string): void;
  unload(): void;
  /** Queued turns plus the generation in flight. */
  depth(): number;
}

type Outcome = "pending" | "timeout" | "unloaded" | "superseded" | "cancelled";

interface Job {
  turn: BrainTurn;
  timeoutMs: number;
  controller: AbortController;
  outcome: Outcome;
  status: "queued" | "running" | "done";
  claimed: boolean;
  resolve: (result: BrainResult) => void;
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

function dropped(reason: string): BrainResult {
  return {
    status: "dropped",
    text: "",
    source: "none",
    fallbackReason: reason,
    attempted: true,
    prompt: [],
    ms: 0,
  };
}

export function createBrainPool(options: BrainPoolOptions): BrainPool {
  const engine = options.engine;
  const classic = options.classic;
  const defaultTimeout = options.timeoutMs ?? DEFAULT_BRAIN_TIMEOUT_MS;
  const queue: Job[] = [];
  const live = new Set<Job>();
  let active: Job | null = null;
  let pumping = false;

  function claim(job: Job): boolean {
    if (job.claimed || job.status === "done") return false;
    job.claimed = true;
    return true;
  }

  function settle(job: Job, result: BrainResult): void {
    if (job.status === "done") return;
    job.status = "done";
    live.delete(job);
    if (active === job) active = null;
    job.resolve(result);
  }

  async function classicResult(
    turn: BrainTurn,
    prompt: PromptMessage[],
    reason: string,
    started: number,
    attempted: boolean,
  ): Promise<BrainResult> {
    let text = "...";
    let source: BrainClassicSource = "fallback";
    let note = reason;
    try {
      const reply = await classic(turn);
      const trimmed = reply.text.trim();
      if (trimmed.length > 0) {
        text = trimmed;
        source = reply.source;
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      note = `${reason} Classic engine failed (${message}).`;
    }
    return {
      status: "fallback",
      text,
      source,
      fallbackReason: note,
      attempted,
      prompt,
      ms: roundMs(performance.now() - started),
    };
  }

  async function resultFor(job: Job, prompt: PromptMessage[], started: number): Promise<BrainResult> {
    if (job.outcome === "timeout") {
      return classicResult(job.turn, prompt, BRAIN_REASONS.timeout, started, true);
    }
    if (job.outcome === "unloaded") {
      return classicResult(job.turn, prompt, BRAIN_REASONS.unload, started, true);
    }
    if (job.outcome === "superseded") return dropped(BRAIN_REASONS.superseded);
    return dropped(BRAIN_REASONS.cancelled);
  }

  function callEngine(job: Job, prompt: PromptMessage[]): Promise<BrainCompletion> {
    try {
      return Promise.resolve(engine.complete({
        messages: prompt,
        temperature: job.turn.temperature,
        maxTokens: job.turn.maxTokens,
        signal: job.controller.signal,
      }));
    } catch (error) {
      return Promise.reject(error);
    }
  }

  async function runOne(job: Job): Promise<void> {
    if (job.status === "done" || job.claimed) return;
    const started = performance.now();
    if (job.outcome !== "pending" || job.controller.signal.aborted) {
      if (!claim(job)) return;
      settle(job, await resultFor(job, [], started));
      return;
    }
    const packed = packDinoPrompt(job.turn);
    const timer = setTimeout(() => {
      if (job.status === "done" || job.outcome !== "pending") return;
      job.outcome = "timeout";
      job.controller.abort();
    }, job.timeoutMs);
    const completion = callEngine(job, packed.prompt);
    const settledEngine = completion.then(() => undefined, () => undefined);
    const aborted = new Promise<void>((resolve) => {
      if (job.controller.signal.aborted) resolve();
      else job.controller.signal.addEventListener("abort", () => resolve(), { once: true });
    });
    try {
      const winner = await Promise.race([
        completion.then(
          (value) => ({ kind: "ok" as const, value }),
          (error: unknown) => ({ kind: "error" as const, error }),
        ),
        aborted.then(() => ({ kind: "abort" as const })),
      ]);
      if (!claim(job)) return;
      if (job.outcome !== "pending") {
        settle(job, await resultFor(job, packed.prompt, started));
        return;
      }
      if (winner.kind === "ok") {
        const text = winner.value.text.trim();
        if (text.length === 0) {
          settle(job, await classicResult(job.turn, packed.prompt, BRAIN_REASONS.empty, started, true));
          return;
        }
        settle(job, {
          status: "model",
          text,
          source: "webllm",
          attempted: true,
          prompt: packed.prompt,
          ms: roundMs(performance.now() - started),
          promptTokens: winner.value.promptTokens,
          completionTokens: winner.value.completionTokens,
          tokensPerSec: winner.value.tokensPerSec,
          prefillTokensPerSec: winner.value.prefillTokensPerSec,
          runtimeStatsText: winner.value.runtimeStatsText,
        });
        return;
      }
      if (winner.kind === "abort") {
        settle(job, await resultFor(job, packed.prompt, started));
        return;
      }
      const message = winner.error instanceof Error ? winner.error.message : String(winner.error);
      settle(job, await classicResult(
        job.turn,
        packed.prompt,
        `WebLLM failed (${message}). The classic engine speaks.`,
        started,
        true,
      ));
    } finally {
      clearTimeout(timer);
      await settledEngine;
    }
  }

  async function pump(): Promise<void> {
    if (pumping) return;
    pumping = true;
    try {
      while (queue.length > 0) {
        const job = queue.shift();
        if (!job || job.status === "done" || job.claimed) continue;
        active = job;
        job.status = "running";
        await runOne(job);
        if (active === job) active = null;
      }
    } finally {
      pumping = false;
      if (active?.status === "done") active = null;
      if (queue.some((job) => job.status !== "done" && !job.claimed)) void pump();
    }
  }

  function enqueue(turn: BrainTurn, timeoutMs: number, resolve: (result: BrainResult) => void): Job {
    const job: Job = {
      turn,
      timeoutMs,
      controller: new AbortController(),
      outcome: "pending",
      status: "queued",
      claimed: false,
      resolve,
    };
    const slot = queue.findIndex((item) => item.turn.dinoId === turn.dinoId && item.status !== "done");
    if (slot >= 0) {
      const old = queue[slot]!;
      old.outcome = "superseded";
      old.controller.abort();
      if (claim(old)) settle(old, dropped(BRAIN_REASONS.superseded));
      queue[slot] = job;
    } else {
      queue.push(job);
    }
    if (active && active.turn.dinoId === turn.dinoId && active.status !== "done" && active.outcome === "pending") {
      active.outcome = "superseded";
      active.controller.abort();
    }
    live.add(job);
    return job;
  }

  return {
    ask(turn, askOptions) {
      if (askOptions?.signal?.aborted) return Promise.resolve(dropped(BRAIN_REASONS.cancelled));
      if (!engine.loadedModelId) {
        const packed = packDinoPrompt(turn);
        return classicResult(turn, packed.prompt, BRAIN_REASONS.notLoaded, performance.now(), false);
      }
      const timeoutMs = askOptions?.timeoutMs ?? defaultTimeout;
      return new Promise((resolve) => {
        const job = enqueue(turn, timeoutMs, resolve);
        const signal = askOptions?.signal;
        if (signal) {
          const onAbort = () => {
            if (job.status === "done" || job.outcome !== "pending") return;
            job.outcome = "cancelled";
            job.controller.abort();
            const index = queue.indexOf(job);
            if (index >= 0) queue.splice(index, 1);
            if (job.status === "queued" && claim(job)) settle(job, dropped(BRAIN_REASONS.cancelled));
          };
          signal.addEventListener("abort", onAbort, { once: true });
        }
        void pump();
      });
    },

    cancel(dinoId) {
      const queued = queue.filter((job) => job.turn.dinoId === dinoId);
      for (let i = queue.length - 1; i >= 0; i -= 1) {
        if (queue[i]?.turn.dinoId === dinoId) queue.splice(i, 1);
      }
      for (const job of queued) {
        job.outcome = "cancelled";
        job.controller.abort();
        if (claim(job)) settle(job, dropped(BRAIN_REASONS.cancelled));
      }
      if (active && active.turn.dinoId === dinoId && active.status !== "done" && active.outcome === "pending") {
        active.outcome = "cancelled";
        active.controller.abort();
      }
    },

    unload() {
      const queued = queue.splice(0, queue.length);
      for (const job of queued) {
        job.outcome = "unloaded";
        job.controller.abort();
        if (claim(job)) {
          const started = performance.now();
          void classicResult(job.turn, [], BRAIN_REASONS.unload, started, true).then((result) => {
            settle(job, result);
          });
        }
      }
      if (active && active.status !== "done") {
        active.outcome = "unloaded";
        active.controller.abort();
      }
    },

    depth() {
      let count = 0;
      for (const job of live) {
        if (job.status !== "done") count += 1;
      }
      return count;
    },
  };
}
