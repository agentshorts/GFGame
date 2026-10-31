import { describe, expect, it } from "vitest";
import { messageTokenEstimate } from "../src/core/memory";
import {
  BRAIN_CONTEXT_CAP,
  BRAIN_REASONS,
  createBrainPool,
  packDinoPrompt,
  type BrainTurn,
} from "../src/logic/brainPool";
import { tokenTotal } from "../src/logic/context";

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

describe("dino window packing", () => {
  it("keeps one dino's memory out of another dino's prompt", () => {
    const grumble = packDinoPrompt(privateTurn("grumble"));
    const tusksaw = packDinoPrompt(privateTurn("tusksaw"));
    const grumbleText = grumble.prompt.map((message) => message.content).join("\n");
    const tusksawText = tusksaw.prompt.map((message) => message.content).join("\n");
    ownsOnly(grumbleText, "grumble", "tusksaw");
    ownsOnly(tusksawText, "tusksaw", "grumble");
  });

  it("respects the 4096 token cap", () => {
    const huge = "abcd".repeat(20_000);
    const packed = packDinoPrompt(turn("cap", {
      personaBlock: huge,
      dinoSummary: huge,
      relationshipSummary: huge,
      worldNote: huge,
      playerRecord: huge,
      elder: true,
      budget: 8000,
      replyReserve: 80,
      recentTurns: Array.from({ length: 12 }, () => ({ speaker: "Pip", role: "user" as const, text: huge })),
    }));
    expect(tokenTotal(packed.state)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP);
    expect(messageTokenEstimate(packed.prompt)).toBeLessThanOrEqual(BRAIN_CONTEXT_CAP);
    expect(messageTokenEstimate(packed.prompt)).toBeGreaterThan(0);
  });

  it("speaks with the classic engine and does not call a model", async () => {
    const pool = createBrainPool({
      classic: async () => ({ text: "mip mip", source: "creature" }),
    });
    const spoken = await pool.ask(turn("nipper"));
    expect(spoken.status).toBe("classic");
    expect(spoken.source).toBe("creature");
    expect(spoken.text).toBe("mip mip");
    expect(spoken.attempted).toBe(false);
    const empty = await createBrainPool({
      classic: async () => ({ text: "   ", source: "eliza" }),
    }).ask(turn("nipper"));
    expect(empty.status).toBe("fallback");
    expect(empty.fallbackReason).toBe(BRAIN_REASONS.empty);
    const failed = await createBrainPool({
      classic: async () => {
        throw new Error("nope");
      },
    }).ask(turn("nipper"));
    expect(failed.fallbackReason).toContain("nope");
    expect(pool.depth()).toBe(0);
  });
});
