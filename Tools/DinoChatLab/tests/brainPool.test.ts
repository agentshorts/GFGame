import { describe, expect, it } from "vitest";
import { messageTokenEstimate } from "../src/core/memory";
import {
  BRAIN_CONTEXT_CAP,
  BRAIN_REASONS,
  createBrainPool,
  packDinoPrompt,
  type BrainClassicReply,
  type BrainEngine,
  type BrainResult,
  type BrainTurn,
} from "../src/logic/brainPool";
import { tokenTotal } from "../src/logic/context";
import { buildChatTrace, idleLlm } from "../src/logic/trace";

function turn(dinoId: string, patch: Partial<BrainTurn> = {}): BrainTurn {
  return {
    dinoId,
    personaName: dinoId,
    personaBlock: `You are ${dinoId}.`,
    worldNote: "",
    dinoSummary: "",
    relationshipSummary: "",
    subjectName: "Pip",
    elder: false,
    playerRecord: "",
    recentTurns: [],
    temperature: 0.2,
    maxTokens: 80,
    utterance: "hello",
    ...patch,
  };
}

function privateTurn(id: string): BrainTurn {
  return turn(id, {
    personaName: id,
    personaBlock: `You are ${id}.`,
    dinoSummary: `SECRET-${id}`,
    relationshipSummary: `REL-${id}`,
    worldNote: `WORLD-${id}`,
    playerRecord: `RECORD-${id}`,
    elder: true,
    recentTurns: [{ speaker: "Pip", role: "user", text: `TURN-${id}` }],
  });
}

function ownsOnly(prompt: string, id: string, other: string): void {
  for (const mark of ["SECRET", "REL", "WORLD", "RECORD", "TURN"]) {
    expect(prompt).toContain(`${mark}-${id}`);
    expect(prompt).not.toContain(`${mark}-${other}`);
  }
}

async function flush(): Promise<void> {
  await new Promise((resolve) => setTimeout(resolve, 0));
  await new Promise((resolve) => setTimeout(resolve, 0));
}

interface HeldCall {
  prompt: string;
  release: (text: string) => void;
}

function holdEngine() {
  const started: string[] = [];
  const waiting: HeldCall[] = [];
  let inFlight = 0;
  let maxInFlight = 0;
  const engine: BrainEngine = {
    loadedModelId: "fake-brain",
    complete(args) {
      const prompt = args.messages.map((message) => message.content).join("\n");
      started.push(prompt);
      inFlight += 1;
      maxInFlight = Math.max(maxInFlight, inFlight);
      return new Promise((resolve, reject) => {
        const finish = (text: string) => {
          inFlight = Math.max(0, inFlight - 1);
          resolve({ text });
        };
        const onAbort = () => {
          inFlight = Math.max(0, inFlight - 1);
          const index = waiting.findIndex((item) => item.prompt === prompt);
          if (index >= 0) waiting.splice(index, 1);
          reject(new Error("aborted"));
        };
        if (args.signal.aborted) {
          onAbort();
          return;
        }
        args.signal.addEventListener("abort", onAbort, { once: true });
        waiting.push({
          prompt,
          release: (text) => {
            args.signal.removeEventListener("abort", onAbort);
            finish(text);
          },
        });
      });
    },
  };
  return {
    engine,
    started,
    waiting,
    maxInFlight: () => maxInFlight,
  };
}

async function releaseNext(gate: ReturnType<typeof holdEngine>, text = "from the model"): Promise<void> {
  const next = gate.waiting.shift();
  if (!next) throw new Error("no generation is in flight");
  next.release(text);
  await flush();
}

function classicReply(text = "classic mouth"): () => Promise<BrainClassicReply> {
  return async () => ({ text, source: "eliza" });
}

function traceOf(result: BrainResult) {
  const source = result.source === "eliza" || result.source === "aiml"
    || result.source === "rivescript" || result.source === "megahal" || result.source === "fallback"
    ? result.source
    : "eliza";
  return buildChatTrace({
    id: "tr-brain",
    at: "2026-10-31T00:00:00.000Z",
    dinoId: "a",
    dinoName: "A",
    inputText: "hello",
    candidates: [{ source, text: result.text, ms: result.ms }],
    chosenSource: result.status === "model" ? "webllm" : source,
    finalText: result.text || "...",
    llm: idleLlm({ persona: "A", memory: "", context: "budget 4096" }),
    fallbackReason: result.fallbackReason,
  });
}

describe("shared brain pool", () => {
  it("keeps one dino's memory out of another dino's prompt", async () => {
    const gate = holdEngine();
    const pool = createBrainPool({ engine: gate.engine, classic: classicReply(), timeoutMs: 3000 });
    const grumble = pool.ask(privateTurn("grumble"));
    const tusksaw = pool.ask(privateTurn("tusksaw"));
    expect(gate.started).toHaveLength(1);
    ownsOnly(gate.started[0] ?? "", "grumble", "tusksaw");
    await releaseNext(gate, "grumble line");
    ownsOnly(gate.started[1] ?? "", "tusksaw", "grumble");
    expect(gate.maxInFlight()).toBe(1);
    await releaseNext(gate, "tusksaw line");
    await expect(grumble).resolves.toMatchObject({ status: "model", text: "grumble line" });
    await expect(tusksaw).resolves.toMatchObject({ status: "model", text: "tusksaw line" });
    expect(pool.depth()).toBe(0);
  });

  it("respects the 4096 token cap", async () => {
    const huge = "abcd".repeat(20_000);
    const packedTurn = turn("cap", {
      personaBlock: huge,
      dinoSummary: huge,
      relationshipSummary: huge,
      worldNote: huge,
      playerRecord: huge,
      elder: true,
      budget: 8000,
      replyReserve: 80,
      recentTurns: Array.from({ length: 12 }, () => ({ speaker: "Pip", role: "user" as const, text: huge })),
    });
    const packed = packDinoPrompt(packedTurn);
    expect(tokenTotal(packed.state)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP);
    expect(messageTokenEstimate(packed.prompt)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP);
    expect(messageTokenEstimate(packed.prompt)).toBeGreaterThan(0);

    const seen: string[] = [];
    const engine: BrainEngine = {
      loadedModelId: "fake-brain",
      async complete(args) {
        seen.push(args.messages.map((message) => message.content).join("\n"));
        expect(messageTokenEstimate(args.messages)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP);
        return { text: "short" };
      },
    };
    const pool = createBrainPool({ engine, classic: classicReply() });
    const result = await pool.ask(packedTurn);
    expect(result.status).toBe("model");
    expect(seen).toHaveLength(1);
    expect(messageTokenEstimate(result.prompt)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP - 80);
  });

  it("serves dinos FIFO, one generation at a time", async () => {
    const gate = holdEngine();
    let classicCalls = 0;
    const pool = createBrainPool({
      engine: gate.engine,
      timeoutMs: 3000,
      classic: async () => {
        classicCalls += 1;
        return { text: "classic mouth", source: "eliza" };
      },
    });
    const asks = ["alpha", "bravo", "charlie"].map((id) => pool.ask(turn(id, { personaBlock: `DINO-${id}` })));
    expect(gate.started).toHaveLength(1);
    expect(gate.started[0]).toContain("DINO-alpha");
    expect(pool.depth()).toBe(3);
    expect(gate.maxInFlight()).toBe(1);
    await releaseNext(gate);
    expect(gate.started).toHaveLength(2);
    expect(gate.started[1]).toContain("DINO-bravo");
    expect(gate.maxInFlight()).toBe(1);
    await releaseNext(gate);
    expect(gate.started[2]).toContain("DINO-charlie");
    await releaseNext(gate);
    const results = await Promise.all(asks);
    expect(results.every((result) => result.status === "model")).toBe(true);
    expect(classicCalls).toBe(0);
    expect(pool.depth()).toBe(0);
  });

  it("replaces a queued dino's slot with the newest line", async () => {
    const gate = holdEngine();
    const pool = createBrainPool({ engine: gate.engine, classic: classicReply(), timeoutMs: 3000 });
    const blocker = pool.ask(turn("blocker", { personaBlock: "DINO-blocker" }));
    const first = pool.ask(turn("alpha", { personaBlock: "DINO-alpha", dinoSummary: "SECRET-FIRST" }));
    const bravo = pool.ask(turn("bravo", { personaBlock: "DINO-bravo" }));
    const second = pool.ask(turn("alpha", { personaBlock: "DINO-alpha", dinoSummary: "SECRET-SECOND" }));
    await expect(first).resolves.toMatchObject({ status: "dropped", fallbackReason: BRAIN_REASONS.superseded });
    expect(pool.depth()).toBe(3);
    expect(gate.started).toHaveLength(1);
    await releaseNext(gate, "blocker line");
    expect(gate.started[1]).toContain("SECRET-SECOND");
    expect(gate.started[1]).not.toContain("SECRET-FIRST");
    expect(gate.started[1]).toContain("DINO-alpha");
    await releaseNext(gate, "alpha line");
    expect(gate.started[2]).toContain("DINO-bravo");
    expect(gate.started).toHaveLength(3);
    await releaseNext(gate, "bravo line");
    await blocker;
    await expect(second).resolves.toMatchObject({ status: "model", text: "alpha line" });
    await expect(bravo).resolves.toMatchObject({ status: "model", text: "bravo line" });
  });

  it("does not let a running dino cut in front of dinos already queued", async () => {
    const gate = holdEngine();
    const pool = createBrainPool({ engine: gate.engine, classic: classicReply(), timeoutMs: 3000 });
    const running = pool.ask(turn("alpha", { personaBlock: "DINO-alpha", dinoSummary: "SECRET-FIRST" }));
    expect(gate.started).toHaveLength(1);
    const bravo = pool.ask(turn("bravo", { personaBlock: "DINO-bravo" }));
    const again = pool.ask(turn("alpha", { personaBlock: "DINO-alpha-again", dinoSummary: "SECRET-SECOND" }));
    await flush();
    await expect(running).resolves.toMatchObject({ status: "dropped", fallbackReason: BRAIN_REASONS.superseded });
    expect(gate.started[1]).toContain("DINO-bravo");
    expect(gate.started.some((prompt) => prompt.includes("SECRET-SECOND"))).toBe(false);
    await releaseNext(gate, "bravo line");
    expect(gate.started[2]).toContain("SECRET-SECOND");
    expect(gate.started[2]).toContain("DINO-alpha-again");
    expect(gate.maxInFlight()).toBe(1);
    await releaseNext(gate, "second line");
    await bravo;
    await again;
  });

  it("falls back to the classic engine when the shared brain times out", async () => {
    let classicCalls = 0;
    let aborted = false;
    const engine: BrainEngine = {
      loadedModelId: "fake-brain",
      complete(args) {
        return new Promise((_resolve, reject) => {
          args.signal.addEventListener("abort", () => {
            aborted = true;
            reject(new Error("aborted"));
          });
        });
      },
    };
    const pool = createBrainPool({
      engine,
      timeoutMs: 30,
      classic: async () => {
        classicCalls += 1;
        return { text: "ELIZA still has the mouth", source: "eliza" };
      },
    });
    const result = await pool.ask(turn("alpha"));
    expect(aborted).toBe(true);
    expect(classicCalls).toBe(1);
    expect(result).toMatchObject({
      status: "fallback",
      text: "ELIZA still has the mouth",
      source: "eliza",
      attempted: true,
      fallbackReason: BRAIN_REASONS.timeout,
    });
    const trace = traceOf(result);
    expect(trace.decisions.find((row) => row.stage === "fallback")?.reason).toBe(BRAIN_REASONS.timeout);
    expect(trace.reason).toContain(BRAIN_REASONS.timeout);
    expect(pool.depth()).toBe(0);
  });

  it("falls back when no model is loaded and does not call the engine", async () => {
    let completes = 0;
    const engine: BrainEngine = {
      loadedModelId: null,
      complete() {
        completes += 1;
        return Promise.resolve({ text: "nope" });
      },
    };
    const pool = createBrainPool({
      engine,
      classic: async () => ({ text: "script line", source: "megahal" }),
    });
    const result = await pool.ask(privateTurn("grumble"));
    expect(completes).toBe(0);
    expect(result).toMatchObject({
      status: "fallback",
      text: "script line",
      source: "megahal",
      attempted: false,
      fallbackReason: BRAIN_REASONS.notLoaded,
    });
    expect(result.prompt.map((message) => message.content).join("\n")).toContain("SECRET-grumble");
    expect(pool.depth()).toBe(0);
  });

  it("drains the queue on unload", async () => {
    const gate = holdEngine();
    let classicCalls = 0;
    const pool = createBrainPool({
      engine: gate.engine,
      timeoutMs: 3000,
      classic: async () => {
        classicCalls += 1;
        return { text: "unloaded mouth", source: "eliza" };
      },
    });
    const first = pool.ask(turn("alpha", { personaBlock: "DINO-alpha" }));
    await flush();
    const second = pool.ask(turn("bravo", { personaBlock: "DINO-bravo" }));
    const third = pool.ask(turn("charlie", { personaBlock: "DINO-charlie" }));
    expect(pool.depth()).toBe(3);
    expect(gate.started).toHaveLength(1);
    pool.unload();
    const results = await Promise.all([first, second, third]);
    expect(pool.depth()).toBe(0);
    expect(gate.started).toHaveLength(1);
    expect(gate.started[0]).toContain("DINO-alpha");
    expect(classicCalls).toBe(3);
    for (const result of results) {
      expect(result).toMatchObject({
        status: "fallback",
        text: "unloaded mouth",
        source: "eliza",
        fallbackReason: BRAIN_REASONS.unload,
      });
    }
    const later = pool.ask(turn("delta", { personaBlock: "DINO-delta" }));
    await flush();
    expect(gate.started.some((prompt) => prompt.includes("DINO-delta"))).toBe(true);
    await releaseNext(gate, "back");
    await expect(later).resolves.toMatchObject({ status: "model", text: "back" });
  });

  it("cancels a queued dino without a classic line", async () => {
    const gate = holdEngine();
    let classicCalls = 0;
    const pool = createBrainPool({
      engine: gate.engine,
      timeoutMs: 3000,
      classic: async () => {
        classicCalls += 1;
        return { text: "classic mouth", source: "eliza" };
      },
    });
    const alpha = pool.ask(turn("alpha", { personaBlock: "DINO-alpha" }));
    const bravo = pool.ask(turn("bravo", { personaBlock: "DINO-bravo" }));
    pool.cancel("bravo");
    await expect(bravo).resolves.toMatchObject({ status: "dropped", fallbackReason: BRAIN_REASONS.cancelled });
    expect(classicCalls).toBe(0);
    expect(gate.started).toHaveLength(1);
    await releaseNext(gate, "alpha line");
    await expect(alpha).resolves.toMatchObject({ status: "model", text: "alpha line" });
    expect(gate.started.some((prompt) => prompt.includes("DINO-bravo"))).toBe(false);
    expect(pool.depth()).toBe(0);
  });
});
