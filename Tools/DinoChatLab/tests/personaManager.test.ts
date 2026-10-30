import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { canSpeak, clampSummary, defaultKnobs, estimateTokens, fallbackSummarize, memoryKeyValueStore, REP_BAND_THRESHOLDS, repBand } from "../src/core";
import { WageCreditError } from "../src/core/stats";
import { gateAction, scoreInterest } from "../src/manager/interest";
import { blankDino, exportDocument, importDocument, personaForTest, profileFromPersona } from "../src/manager/profile";
import { createLocalStorage } from "../src/manager/storage";
import { runManagerTurn } from "../src/manager/testChat";
import { parsePersona } from "../src/logic/persona";
import grumbleJson from "../personas/01-big-grumble.json";

describe("persona manager", () => {
  it("builds a draft the existing persona parser accepts", () => {
    const dino = blankDino(new Set(["new-dino"]));
    expect(dino.persona.id).not.toBe("new-dino");
    expect(personaForTest(dino).id).toBe(dino.persona.id);
    const again = importDocument(JSON.parse(exportDocument(dino)), new Set());
    expect(again.persona.name).toBe(dino.persona.name);
    expect(again.profile.speechPattern).toBe("CASUAL");
  });

  it("keeps bundled export compatible with parsePersona", () => {
    const persona = parsePersona(grumbleJson);
    const dino = { persona, profile: profileFromPersona(persona) };
    const exported = JSON.parse(exportDocument(dino)) as unknown;
    expect(parsePersona(exported).id).toBe("big-grumble");
  });

  it("scores interest the way the test chat explains it", () => {
    const dino = blankDino(new Set());
    dino.profile.topics.push({ name: "mango", sentiment: "like" });
    dino.profile.relationships.push({ subjectKey: "Pip", kind: "rival", affinity: -4 });
    expect(scoreInterest("hello", dino.profile, "Pip").interest).toBe(7);
    expect(scoreInterest("hello", dino.profile, "Pip").action).toBe("reply");
    expect(scoreInterest("I brought mango", dino.profile, "Sam").interest).toBe(7);
    expect(scoreInterest("hello", dino.profile, "Sam", REP_BAND_THRESHOLDS.friendlyAt).band).toBe("friendly");
    expect(scoreInterest("hello", dino.profile, "Sam", REP_BAND_THRESHOLDS.friendlyAt).interest).toBe(6);
    dino.profile.interest.comment = 7;
    expect(gateAction(7, dino.profile.interest)).toBe("like");
    expect(gateAction(6, dino.profile.interest)).toBe("ignore");
    expect(scoreInterest("mango mango mango mango", dino.profile, "Pip").interest).toBe(9);
    expect(repBand(REP_BAND_THRESHOLDS.hostileAt, REP_BAND_THRESHOLDS)).toBe("hostile");
  });

  it("records a quota miss without calling it a reply", () => {
    const quota = { ...defaultKnobs().quota, speakChance: 0 };
    const missed = canSpeak({
      dinoId: "new-dino",
      subjectKey: "Pip",
      log: [],
      now: "2026-10-30T00:00:00.000Z",
      quota,
    });
    expect(missed.ok).toBe(false);
    expect(canSpeak({
      dinoId: "new-dino",
      subjectKey: "Pip",
      log: [],
      now: "2026-10-30T00:00:00.000Z",
      quota: defaultKnobs().quota,
    }).reason).toBe("ok");
  });

  it("uses the core summary and stays deterministic", () => {
    const turn = { speaker: "Pip", text: "mango grudge ".repeat(40) };
    const once = fallbackSummarize("old fact", [turn], 10);
    const twice = fallbackSummarize("old fact", [turn], 10);
    expect(twice).toBe(once);
    expect(estimateTokens(once)).toBeLessThanOrEqual(10);
    expect(clampSummary("abcdef", 1)).toBe("abcd");
  });

  it("round-trips personas, memory, wages, and stat totals through the core adapter", async () => {
    const storage = createLocalStorage(memoryKeyValueStore());
    const dino = blankDino(new Set());
    const json = JSON.stringify(dino);
    const saved = await storage.savePersona({ id: dino.persona.id, owner: "ada", version: 0, json });
    expect(saved.version).toBe(1);
    expect(saved.owner).toBe("ada");
    await expect(storage.savePersona({ id: dino.persona.id, owner: "ada", version: 0, json })).rejects.toMatchObject({ status: 409 });
    const updated = await storage.savePersona({ id: dino.persona.id, owner: "ada", version: 1, json });
    expect(updated.version).toBe(2);
    await storage.putMemory({ dinoId: dino.persona.id, summary: "Pip brought mango.", updatedAt: "2026-10-30T00:00:00.000Z", turnsSinceSummary: 1 });
    expect((await storage.getMemory(dino.persona.id))?.summary).toContain("mango");
    const event = await storage.bumpStats({
      at: "2026-10-30T00:00:00.000Z",
      playerKey: "ada",
      familyId: "red",
      dinoId: dino.persona.id,
      kind: "kill",
      amount: 2,
      note: "lab",
    });
    expect(event.amount).toBe(2);
    const rows = await storage.getStats("ada");
    expect(rows.find((row) => row.familyId === "red")?.kills).toBe(2);
    expect(rows.find((row) => row.familyId === "*")?.kills).toBe(2);
    expect(rows.find((row) => row.familyId === "red")?.rep).toBe(-16);
    expect((await storage.creditWages(dino.persona.id, 10)).lifetimeEarnings).toBe(10);
    expect((await storage.creditWages(dino.persona.id, 5)).lifetimeEarnings).toBe(15);
    await expect(storage.creditWages(dino.persona.id, -1)).rejects.toBeInstanceOf(WageCreditError);
  });

  it("talks in the test chat and writes a decision row", async () => {
    const dino = blankDino(new Set());
    const turn = await runManagerTurn({
      dino,
      playerName: "Pip",
      text: "hello",
      dinoSummary: "",
      relationshipSummary: "",
      turnsSinceSummary: 0,
      interactions: 0,
      cycleId: "cycle-test",
      now: new Date("2026-10-30T12:00:00.000Z"),
    });
    expect(turn.log.action).toBe("reply");
    expect(turn.log.metadata.interest).toBe(5);
    expect(turn.log.metadata.quota).toBe("ok");
    expect(turn.log.metadata.memoryDelta).toContain("dino summary updated");
    expect(turn.lines.some((line) => line.role === "dino" && line.text.length > 0)).toBe(true);
    expect(turn.dinoSummary.length).toBeGreaterThan(0);

    dino.profile.interest.comment = 10;
    const quiet = await runManagerTurn({
      dino,
      playerName: "Pip",
      text: "hello",
      dinoSummary: "",
      relationshipSummary: "",
      turnsSinceSummary: 0,
      interactions: 0,
      cycleId: "cycle-test",
    });
    expect(quiet.log.action).toBe("ignore");
    expect(quiet.lines.some((line) => line.role === "dino")).toBe(false);
  });
});

describe("wrangler config", () => {
  it("names both sqlite classes and carries no account or secret", () => {
    const text = readFileSync(new URL("../wrangler.jsonc", import.meta.url), "utf8");
    const stripped = text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/^\s*\/\/.*$/gm, "");
    const config = JSON.parse(stripped) as {
      name: string;
      account_id?: string;
      routes?: unknown;
      migrations: { new_sqlite_classes: string[] }[];
      durable_objects: { bindings: { name: string; class_name: string }[] };
      observability: { enabled: boolean; traces: { enabled: boolean } };
    };
    expect(config.name).toBe("dino-chat-lab");
    expect(config.account_id).toBeUndefined();
    expect(config.routes).toBeUndefined();
    expect(config.migrations[0]?.new_sqlite_classes).toEqual(["PersonaHub", "SignupLimiter"]);
    expect(config.durable_objects.bindings.map((binding) => binding.class_name)).toEqual(["PersonaHub", "SignupLimiter"]);
    expect(config.observability.enabled).toBe(true);
    expect(config.observability.traces.enabled).toBe(true);
    expect(text).not.toMatch(/api[_-]?key|secret|token|account_id/i);
  });
});
