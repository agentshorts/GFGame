import { beforeEach, describe, expect, it } from "vitest";
import { memoryNote, NOTE_TOKEN_CAP, stillNoted } from "../src/dinobook";
import { alignmentDecision, decide } from "../src/logic/alignment";
import { estimateTokens } from "../src/logic/context";
import { presentReply } from "../src/logic/present";
import { pickSteer, steerDecision } from "../src/logic/steer";
import {
  buildChatTrace,
  clearTraces,
  filterTraces,
  listTraces,
  mergeCandidates,
  nextTraceId,
  probeClassic,
  recordTrace,
  setTraceEnabled,
  templateCandidates,
  tracesToJson,
} from "../src/logic/trace";

beforeEach(() => {
  clearTraces();
  setTraceEnabled(true);
});

describe("present trace", () => {
  it("records fill, filter, and the clip as decisions", () => {
    const shown = presentReply("Where you going, {Target}? that is shit", {
      playerName: "Pip",
      maxReplyLength: 200,
      maxTokens: 80,
    });
    expect(shown.rawText).toContain("Pip");
    expect(shown.decisions.map((row) => row.stage)).toEqual(["fillTarget", "filter", "present"]);
    expect(shown.decisions[0]?.reason).toContain("Pip");
    expect(shown.decisions[1]?.reason.toLowerCase()).toContain("filter");
    expect(shown.filtered).toBe(true);
  });

  it("says when there was no token and nothing to hide", () => {
    const shown = presentReply("Why you running?", {
      playerName: "",
      maxReplyLength: 80,
      maxTokens: 40,
    });
    expect(shown.decisions.find((row) => row.stage === "fillTarget")?.reason).toContain("No {Target}");
    expect(shown.decisions.find((row) => row.stage === "filter")?.detail?.filtered).toBe(false);
  });
});

describe("chat trace", () => {
  it("keeps every engine, the prompt, and why the line was picked", () => {
    const shown = presentReply("Why you running, huh?", {
      playerName: "Pip",
      maxReplyLength: 80,
      maxTokens: 40,
    });
    const event = buildChatTrace({
      id: "tr-test",
      at: "2026-10-31T00:00:00.000Z",
      dinoId: "ripwing",
      dinoName: "Ripwing",
      inputText: "why you running",
      candidates: [
        { source: "eliza", text: "Why you running, huh?", ms: 3 },
        { source: "aiml", text: null, ms: 2, note: "none" },
        { source: "rivescript", text: "Dive.", ms: 4 },
        { source: "megahal", text: "Screech running", ms: 11 },
        { source: "webllm", text: null, ms: 0, note: "model not loaded" },
      ],
      chosenSource: "eliza",
      finalText: shown.text,
      presentedDecisions: shown.decisions,
      steer: steerDecision(pickSteer({
        mode: "direct",
        speaker: "Pip",
        userText: "why you running",
        intent: "ignore",
        cues: [{ keywords: ["running"], replies: ["go"] }],
      })),
      alignment: alignmentDecision(decide({
        alignment: "CN",
        inventory: [],
        distanceFt: 40,
        smellRangeFt: 30,
        winOdds: 0.4,
        dinobook: false,
        wants: [{ id: "mango-block", name: "Mango block" }],
      })),
      interest: 6,
      interestReason: "enough to reply",
      llm: {
        text: null,
        ms: 0,
        persona: "You are Ripwing",
        memory: "Pip waved",
        context: "budget 4096\nPip: why you running",
        prompt: "[system]\nYou are Ripwing",
        loaded: false,
      },
      timings: { total: 20 },
    });
    expect(event.kind).toBe("chat");
    expect(event.chosenSource).toBe("eliza");
    expect(event.candidates.find((row) => row.source === "aiml")?.text).toBeNull();
    expect(event.candidates.find((row) => row.source === "eliza")?.score).toBeGreaterThan(0);
    expect(event.llm?.persona).toContain("Ripwing");
    expect(event.llm?.memory).toContain("Pip waved");
    expect(event.llm?.context).toContain("4096");
    expect(event.llm?.prompt).toContain("[system]");
    expect(event.decisions.map((row) => row.stage)).toEqual(
      expect.arrayContaining(["score", "alignment", "steer", "fillTarget", "filter", "present", "pick"]),
    );
    expect(event.timings.eliza).toBe(3);
    expect(event.timings.megahal).toBe(11);
    expect(event.timings.total).toBe(20);
    expect(event.reason).toContain("ELIZA");
  });

  it("records a fallback when the engine came back empty", () => {
    const event = buildChatTrace({
      id: nextTraceId(),
      at: "2026-10-31T00:00:00.000Z",
      dinoId: "tusksaw",
      dinoName: "Tusksaw",
      inputText: "hello",
      candidates: [{ source: "eliza", text: null, ms: 1, note: "none" }],
      chosenSource: "fallback",
      finalText: "...",
      fallbackReason: "The selected engine returned nothing. The persona fallback stands.",
      llm: null,
    });
    expect(event.decisions.some((row) => row.stage === "fallback")).toBe(true);
    expect(event.chosenSource).toBe("fallback");
  });
});

describe("trace log", () => {
  it("filters by dino and source and exports json", () => {
    recordTrace(buildChatTrace({
      id: "a",
      at: "2026-10-31T00:00:01.000Z",
      dinoId: "ripwing",
      dinoName: "Ripwing",
      inputText: "hi",
      candidates: [{ source: "megahal", text: "screech", ms: 5 }],
      chosenSource: "eliza",
      finalText: "dive",
      llm: null,
    }));
    recordTrace({
      id: "b",
      at: "2026-10-31T00:00:02.000Z",
      kind: "payday",
      dinoId: "scythepaw",
      dinoName: "Scythepaw",
      eventLabel: "Payday",
      finalText: "paid",
      chosenSource: "canned",
      reason: "payday",
      candidates: templateCandidates("paid", 1),
      llm: {
        text: null,
        ms: 0,
        persona: "Scythepaw",
        memory: "",
        context: "Event Payday.",
        prompt: "[system]\nScythepaw",
        loaded: false,
      },
      decisions: [{ stage: "payday", reason: "Shift posted." }],
      timings: { template: 1 },
    });
    expect(listTraces()).toHaveLength(2);
    expect(filterTraces(listTraces(), { dinoId: "ripwing" }).map((row) => row.id)).toEqual(["a"]);
    expect(filterTraces(listTraces(), { source: "megahal" }).map((row) => row.id)).toEqual(["a"]);
    expect(filterTraces(listTraces(), { source: "canned" }).map((row) => row.id)).toEqual(["b"]);
    const parsed = JSON.parse(tracesToJson(listTraces(), "2026-10-31T00:00:03.000Z")) as {
      kind: string;
      events: { id: string }[];
    };
    expect(parsed.kind).toBe("dino-chat-traces");
    expect(parsed.events).toHaveLength(2);
    clearTraces();
    expect(listTraces()).toHaveLength(0);
  });
});

describe("engine probe", () => {
  it("keeps a miss as none and times each source", async () => {
    const rows = await probeClassic(async (source) => {
      if (source === "aiml") return { text: "  " };
      if (source === "megahal") throw new Error("nope");
      return { text: `${source} line`, fellBack: source === "rivescript" };
    });
    expect(rows.map((row) => row.source)).toEqual(["eliza", "aiml", "rivescript", "megahal"]);
    expect(rows.find((row) => row.source === "aiml")?.text).toBeNull();
    expect(rows.find((row) => row.source === "megahal")?.note).toBe("nope");
    expect(rows.find((row) => row.source === "rivescript")?.fellBack).toBe(true);
    expect(rows.every((row) => row.ms >= 0)).toBe(true);
  });

  it("merges a probe over the not-run slots", () => {
    const merged = mergeCandidates(templateCandidates("template line", 0.2), [
      { source: "eliza", text: "hello", ms: 4 },
    ]);
    expect(merged.find((row) => row.source === "eliza")?.text).toBe("hello");
    expect(merged.find((row) => row.source === "canned")?.text).toBe("template line");
    expect(merged.find((row) => row.source === "aiml")?.note).toBe("not run");
  });
});

describe("memory note", () => {
  it("strips a nested still-noted tail and stays under the cap", () => {
    const nested = `Lure light. Pip attacked us. Still noted: ${"mango block plan ".repeat(30)}`;
    const note = memoryNote(nested);
    expect(note.toLowerCase()).not.toContain("still noted");
    expect(estimateTokens(note)).toBeLessThanOrEqual(NOTE_TOKEN_CAP);
    const again = stillNoted(`Still noted: ${note} Still noted: older and older`);
    expect(again.match(/still noted/gi)).toHaveLength(1);
    expect(estimateTokens(again)).toBeLessThanOrEqual(NOTE_TOKEN_CAP + 4);
  });
});
