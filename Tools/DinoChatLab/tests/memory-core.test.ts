import { describe, expect, it } from "vitest";
import { decide, type DecisionInput } from "../src/logic/alignment";
import { buildWebLlmMessages } from "../src/logic/prompt";
import {
  assembleTurn,
  blankRelationship,
  budgetSplit,
  canSpeak,
  clampSummary,
  decideContentAction,
  decideInterest,
  fallbackSummarize,
  messageTokenEstimate,
  planSpeak,
  prioritize,
  scoreInterest,
  shouldSummarize,
} from "../src/core/memory";
import {
  affinityDelta,
  applyStatEvent,
  asksForPlayerRecord,
  blankWork,
  mateRank,
  seekingWorkInTerritory,
  statsDigest,
} from "../src/core/stats";
import {
  LocalStorageAdapter,
  SQLITE_SCHEMA,
  StorageConflictError,
  memoryKeyValueStore,
} from "../src/core/storage";
import {
  DEFAULT_CONTEXT_BUDGET,
  defaultKnobs,
  estimateTokens,
  type PlayerStatsRow,
  type RelationshipKind,
  type StatEvent,
} from "../src/core/types";

const knobs = defaultKnobs();

function event(partial: Partial<StatEvent> & Pick<StatEvent, "kind">): StatEvent {
  return {
    id: 1,
    at: "2026-10-30T00:00:00.000Z",
    playerKey: "pip",
    familyId: "pack",
    dinoId: "tusksaw",
    amount: 1,
    note: "",
    ...partial,
  };
}

function world(partial: Partial<DecisionInput> & Pick<DecisionInput, "alignment">): DecisionInput {
  return {
    inventory: [],
    distanceFt: 40,
    smellRangeFt: 30,
    winOdds: 0.4,
    dinobook: false,
    wants: [{ id: "mango-block", name: "Mango block" }],
    ...partial,
  };
}

describe("budget", () => {
  it("splits 4096 into the planned blocks and never goes over", () => {
    for (const total of [128, 256, 4096, 8192]) {
      for (const elder of [true, false]) {
        const split = budgetSplit(total, 80, elder);
        const sum = split.persona + split.world + split.dinoSummary + split.relationship
          + split.elderStats + split.replyReserve + split.recentTurns;
        expect(sum).toBe(split.total);
        expect(split.total).toBeLessThanOrEqual(Math.max(128, total));
        expect(split.total).toBeLessThanOrEqual(8192);
      }
    }
    const full = budgetSplit();
    expect(full.total).toBe(DEFAULT_CONTEXT_BUDGET);
    expect(full.persona).toBe(900);
    expect(full.world).toBe(150);
    expect(full.dinoSummary).toBe(250);
    expect(full.relationship).toBe(150);
    expect(full.elderStats).toBe(200);
    expect(full.replyReserve).toBe(80);
    const quiet = budgetSplit(4096, 80, false);
    expect(quiet.elderStats).toBe(0);
    expect(quiet.recentTurns).toBe(full.recentTurns + 200);
  });

  it("keeps an assembled turn inside the window", () => {
    const turns = Array.from({ length: 30 }, (_, index) => ({
      speaker: index % 2 === 0 ? "Pip" : "Tusksaw",
      role: index % 2 === 0 ? "user" as const : "dino" as const,
      text: `${"mango ".repeat(40)} turn ${index}`,
    }));
    const messages = assembleTurn({
      personaName: "Tusksaw",
      personaBlock: "P ".repeat(5000),
      worldNote: "W ".repeat(2000),
      dinoSummary: "S ".repeat(2000),
      relationshipSummary: "R ".repeat(2000),
      subjectName: "Pip",
      elder: true,
      playerRecord: "kills of our family: 99 | ".repeat(80),
      recentTurns: turns,
      budget: 4096,
      replyReserve: 80,
    });
    expect(messageTokenEstimate(messages)).toBeLessThanOrEqual(4096 - 80);
    expect(messages[0]?.content).toContain("<your_memory>");
    expect(messages[0]?.content).toContain("<you_and_Pip>");
    expect(messages[0]?.content).toContain("<player_record>");
    expect(messages.at(-1)?.content).toContain("turn 29");
    const young = assembleTurn({
      personaName: "Tusksaw",
      personaBlock: "hello",
      worldNote: "",
      dinoSummary: "",
      relationshipSummary: "",
      subjectName: "Pip",
      elder: false,
      playerRecord: "kills of our family: 3",
      recentTurns: [],
      budget: 4096,
      replyReserve: 80,
    });
    expect(young[0]?.content).not.toContain("<player_record>");
    expect(young[0]?.content).not.toContain("kills");
  });

  it("trims WebLLM history to the budget and keeps the newest line", () => {
    const history = Array.from({ length: 40 }, (_, index) => ({
      speaker: index % 2 === 0 ? "Pip" : "Big Grumble",
      role: (index % 2 === 0 ? "user" : "dino") as "user" | "dino",
      text: `turn ${index} ${"word ".repeat(80)}`,
    }));
    const messages = buildWebLlmMessages("You are Big Grumble. ".repeat(100), history, "Big Grumble", 4096, 80);
    expect(messageTokenEstimate(messages)).toBeLessThanOrEqual(4096 - 80);
    expect(messages.at(-1)?.content).toContain("turn 39");
    expect(messages.some((message) => message.content.includes("turn 0"))).toBe(false);
  });
});

describe("summaries", () => {
  it("clamps and stays deterministic", () => {
    const prior = `${"old grudge ".repeat(40)}`;
    const turns = [{ speaker: "Pip", text: "brought mango" }];
    const once = fallbackSummarize(prior, turns, 12);
    const twice = fallbackSummarize(prior, turns, 12);
    expect(once).toBe(twice);
    expect(estimateTokens(once)).toBeLessThanOrEqual(12);
    expect(once).toContain("Pip: brought mango");
    expect(once).not.toContain("old grudge");
    expect(estimateTokens(clampSummary("a".repeat(5000), 250))).toBeLessThanOrEqual(250);
    expect(estimateTokens(clampSummary("b".repeat(5000), 150))).toBeLessThanOrEqual(150);
    const memory = { dinoId: "tusksaw", summary: "", updatedAt: "", turnsSinceSummary: 5 };
    expect(shouldSummarize(memory, 6)).toBe(false);
    expect(shouldSummarize({ ...memory, turnsSinceSummary: 6 }, 6)).toBe(true);
  });
});

describe("interest and quotas", () => {
  it("likes a reply-to-reply unless the other dino is a rival or interest is high", () => {
    const base = {
      thresholds: knobs.thresholds,
      speakerIsDino: true,
      speakerIsSelf: false,
    };
    const quiet = decideInterest({ ...base, interest: 7, relationshipKind: "neutral" });
    expect(quiet.code).toBe("reply_to_reply");
    expect(quiet.action).toBe("like");
    const rival = decideInterest({ ...base, interest: 5, relationshipKind: "rival" });
    expect(rival.action).toBe("reply");
    const hot = decideInterest({ ...base, interest: 8, relationshipKind: "friend" });
    expect(hot.action).toBe("reply");
    const self = decideInterest({ ...base, interest: 9, relationshipKind: "friend", speakerIsSelf: true });
    expect(self.action).toBe("ignore");
    const direct = planSpeak({
      mode: "direct",
      quotaOk: true,
      quotaReason: "ok",
      decision: quiet,
    });
    expect(direct.action).toBe("reply");
    const crowd = planSpeak({
      mode: "all-chat",
      quotaOk: true,
      quotaReason: "ok",
      decision: quiet,
    });
    expect(crowd.logAction).toBe("skip_reply_to_reply");
    const blocked = planSpeak({
      mode: "all-chat",
      quotaOk: false,
      quotaReason: "resting (4s left)",
      decision: rival,
    });
    expect(blocked.logAction).toBe("skip_quota");
    expect(decideContentAction({
      interest: 5,
      thresholds: knobs.thresholds,
      kind: "post",
      speakerIsDino: false,
      speakerIsSelf: false,
      relationshipKind: "neutral",
    }).action).toBe("POST");
    expect(decideContentAction({
      interest: 4,
      thresholds: knobs.thresholds,
      kind: "post",
      speakerIsDino: false,
      speakerIsSelf: false,
      relationshipKind: "neutral",
    }).action).toBe("NONE");
    expect(scoreInterest({ text: "mango", topics: [{ name: "mango", sentiment: "like" }] })).toBe(7);
  });

  it("enforces minute, hour, rest, same-player, and speak-chance limits", () => {
    const now = "2026-10-30T12:00:30.000Z";
    const recent = {
      at: "2026-10-30T12:00:20.000Z",
      dinoId: "tusksaw",
      subjectKey: "pip",
      action: "reply" as const,
    };
    expect(canSpeak({
      dinoId: "tusksaw",
      subjectKey: "pip",
      log: [recent],
      now,
      quota: { ...knobs.quota, maxPerMinute: 1, restSeconds: 0 },
    }).reason).toMatch(/per-minute/);
    expect(canSpeak({
      dinoId: "tusksaw",
      subjectKey: "pip",
      log: [recent],
      now,
      quota: { ...knobs.quota, maxPerMinute: 5, restSeconds: 30 },
    }).reason).toMatch(/resting/);
    expect(canSpeak({
      dinoId: "tusksaw",
      subjectKey: "pip",
      log: [recent, { ...recent, at: "2026-10-30T11:30:00.000Z" }],
      now,
      quota: { ...knobs.quota, maxPerMinute: 5, restSeconds: 0, maxToSamePlayerPerHour: 2 },
    }).reason).toMatch(/same-player/);
    expect(canSpeak({
      dinoId: "tusksaw",
      subjectKey: "pip",
      log: [],
      now,
      quota: { ...knobs.quota, speakChance: 0.2 },
      random: () => 0.9,
    }).ok).toBe(false);
    expect(canSpeak({
      dinoId: "tusksaw",
      subjectKey: "pip",
      log: [],
      now,
      quota: { ...knobs.quota, speakChance: 0.2 },
      random: () => 0.1,
    }).ok).toBe(true);
    expect(canSpeak({
      dinoId: "ripwing",
      subjectKey: "pip",
      log: [recent],
      now,
      quota: knobs.quota,
    }).ok).toBe(true);
  });

  it("orders friends ahead of neutrals and drops ignore", () => {
    const roster = [
      { id: "neutral-dino" },
      { id: "ignored" },
      { id: "friend-dino" },
    ];
    const kinds: Record<string, RelationshipKind> = {
      "neutral-dino": "neutral",
      ignored: "ignore",
      "friend-dino": "friend",
    };
    expect(prioritize(roster, (dino) => kinds[dino.id] ?? "neutral").map((dino) => dino.id)).toEqual([
      "friend-dino",
      "neutral-dino",
    ]);
  });
});

describe("player stats", () => {
  it("aggregates a family row and the totals row, with rep deltas", () => {
    let rows: PlayerStatsRow[] = [];
    rows = applyStatEvent(rows, event({ kind: "kill" }));
    rows = applyStatEvent(rows, event({ kind: "elder_kill", id: 2 }));
    rows = applyStatEvent(rows, event({ kind: "hit_taken", id: 3, amount: 4 }));
    rows = applyStatEvent(rows, event({ kind: "mango_given", id: 4 }));
    rows = applyStatEvent(rows, event({ kind: "mango_refused", id: 5 }));
    rows = applyStatEvent(rows, event({ kind: "block_given", id: 6 }));
    rows = applyStatEvent(rows, event({ kind: "block_refused", id: 7 }));
    rows = applyStatEvent(rows, event({ kind: "rental_returned", id: 8 }));
    rows = applyStatEvent(rows, event({ kind: "rental_late", id: 9 }));
    rows = applyStatEvent(rows, event({ kind: "rental_downed", id: 10 }));
    rows = applyStatEvent(rows, event({ kind: "bounty_claimed", id: 11 }));
    rows = applyStatEvent(rows, event({ kind: "bounty_placed", id: 12 }));
    const family = rows.find((row) => row.familyId === "pack");
    const totals = rows.find((row) => row.familyId === "*");
    expect(family?.kills).toBe(1);
    expect(family?.elderKills).toBe(1);
    expect(family?.hitsTaken).toBe(4);
    expect(family?.mangoGiven).toBe(1);
    expect(family?.mangoRefused).toBe(1);
    expect(family?.blocksGiven).toBe(1);
    expect(family?.blocksRefused).toBe(1);
    expect(family?.rentalsReturned).toBe(1);
    expect(family?.rentalsLate).toBe(1);
    expect(family?.rentalsDowned).toBe(1);
    expect(family?.bountiesClaimed).toBe(1);
    expect(family?.bountiesOnPlayer).toBe(1);
    expect(totals?.kills).toBe(1);
    expect(totals?.elderKills).toBe(1);
    expect(family?.rep).toBe(totals?.rep);
    expect(family?.rep).toBe(-8 - 25 + 3 - 2 + 2 - 1 + 2 - 3 - 6 - 10 - 5);
    expect(affinityDelta("kill")).toBe(-6);
    expect(affinityDelta("mango_given")).toBe(2);
    expect(affinityDelta("mango_refused")).toBe(-1);
    expect(affinityDelta("hit_dealt")).toBe(-3);
  });

  it("keeps the elder digest at or under 200 tokens and hides counts from everyone else", () => {
    const rows = applyStatEvent([], event({ kind: "kill", amount: 3 }));
    const elder = statsDigest({ rows, playerKey: "pip", familyId: "pack", elder: true });
    expect(estimateTokens(elder)).toBeLessThanOrEqual(200);
    expect(elder).toContain("kills of our family: 3");
    expect(elder).toContain("wary");
    const band = statsDigest({ rows, playerKey: "pip", familyId: "pack", elder: false });
    expect(band).toBe("rep_wary");
    expect(band).not.toContain("kills");
    const tight = statsDigest({
      rows,
      playerKey: "pip",
      familyId: "pack",
      elder: true,
      tokenCap: 8,
    });
    expect(estimateTokens(tight)).toBeLessThanOrEqual(8);
    expect(asksForPlayerRecord("what do you think of me")).toBe(true);
    expect(asksForPlayerRecord("why you running")).toBe(false);
  });

  it("lets hostile rep push a threat, and leaves good alignments alone", () => {
    expect(decide(world({ alignment: "NE", repBand: "hostile" })).intent).toBe("attack");
    const carrying = [{ id: "mango-block", name: "Mango block", count: 3 }];
    expect(decide(world({
      alignment: "NE",
      inventory: carrying,
      distanceFt: 12,
      winOdds: 0.8,
      repBand: "friendly",
    })).intent).toBe("barter");
    for (const alignment of ["LG", "NG", "CG"] as const) {
      const result = decide(world({
        alignment,
        inventory: carrying,
        distanceFt: 5,
        winOdds: 1,
        dinobook: true,
        repBand: "hostile",
      }));
      expect(["attack", "ambush", "extort"]).not.toContain(result.intent);
    }
  });
});

describe("local storage", () => {
  it("round-trips memories per dino and rejects a stale persona version", () => {
    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    adapter.putMemory({ dinoId: "tusksaw", summary: "owes a mango", updatedAt: "t", turnsSinceSummary: 2 });
    adapter.putMemory({ dinoId: "ripwing", summary: "likes heights", updatedAt: "t", turnsSinceSummary: 0 });
    adapter.putRelationship(blankRelationship("tusksaw", "pip", "t", "rival"));
    adapter.putRelationship(blankRelationship("tusksaw", "sam", "t", "friend"));
    adapter.putRelationship(blankRelationship("ripwing", "pip", "t", "family"));
    expect(adapter.getMemory("tusksaw")?.summary).toBe("owes a mango");
    expect(adapter.getMemory("ripwing")?.summary).toBe("likes heights");
    expect(adapter.getRelationship("tusksaw", "pip")?.kind).toBe("rival");
    expect(adapter.listRelationships("tusksaw")).toHaveLength(2);
    expect(adapter.listRelationships("ripwing")).toHaveLength(1);

    const created = adapter.savePersona({ id: "custom", owner: "local", version: 0, json: "{\"id\":\"custom\"}" });
    expect(created.version).toBe(1);
    const updated = adapter.savePersona({ id: "custom", owner: "local", version: 1, json: "{\"id\":\"custom\",\"name\":\"Custom\"}" });
    expect(updated.version).toBe(2);
    expect(() => adapter.savePersona({ id: "custom", owner: "local", version: 1, json: "{}" })).toThrow(StorageConflictError);
    try {
      adapter.savePersona({ id: "custom", owner: "local", version: 1, json: "{}" });
    } catch (error) {
      expect(error).toBeInstanceOf(StorageConflictError);
      expect((error as StorageConflictError).status).toBe(409);
    }

    const logged = adapter.appendLog({
      id: "",
      at: "t",
      cycleId: "cycle-1",
      dinoId: "tusksaw",
      subjectKey: "pip",
      action: "reply",
      detail: "interest 6",
      metadata: { interest: 6 },
      engine: "eliza",
      modelId: null,
      promptTokens: null,
      completionTokens: null,
      durationMs: 3,
    });
    expect(logged.id.length).toBeGreaterThan(0);
    expect(adapter.listLog("tusksaw")).toHaveLength(1);

    adapter.bumpStats(event({ kind: "mango_given" }));
    const stats = adapter.getStats("pip");
    expect(stats.find((row) => row.familyId === "pack")?.mangoGiven).toBe(1);
    expect(stats.find((row) => row.familyId === "*")?.rep).toBe(3);

    adapter.putKnobs({ ...knobs, allChatBudget: 2048, quota: { ...knobs.quota, restSeconds: 12 } });
    expect(adapter.getKnobs().allChatBudget).toBe(2048);
    expect(adapter.getKnobs().quota.restSeconds).toBe(12);
    expect(SQLITE_SCHEMA).toContain("CREATE TABLE IF NOT EXISTS player_stats");
    expect(SQLITE_SCHEMA).toContain("CREATE TABLE IF NOT EXISTS stat_events");
    expect(SQLITE_SCHEMA).toContain("CREATE TABLE IF NOT EXISTS decision_log");
    expect(SQLITE_SCHEMA).toContain("PRIMARY KEY (player_key, family_id)");
    expect(SQLITE_SCHEMA).toContain("CREATE TABLE IF NOT EXISTS dino_work");
    expect(SQLITE_SCHEMA).not.toContain("mate_rank");
  });

  it("keeps a factory-work hook and derives mate rank", () => {
    const seeking = { ...blankWork("tusksaw"), seekingJob: true, wagesEarned: 500, mineralsFound: 4 };
    expect(seekingWorkInTerritory(seeking, true)).toBe(true);
    expect(seekingWorkInTerritory(seeking, false)).toBe(false);
    expect(seekingWorkInTerritory({
      ...seeking,
      employerId: "kiln",
      employment: "rented",
    }, true)).toBe(false);
    expect(mateRank({ wagesEarned: 500, statScore: 0, hp: 100, maxHp: 100 })).toBe(3);
    expect(mateRank({ wagesEarned: 9_999, statScore: 80, hp: 100, maxHp: 100 })).toBe(10);
    expect(mateRank({ wagesEarned: 0, statScore: 0, hp: 0, maxHp: 0 })).toBe(0);
    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    adapter.putWork(seeking);
    expect(adapter.getWork("tusksaw")).toMatchObject({ wagesEarned: 500, mineralsFound: 4, seekingJob: true });
    expect(adapter.getWork("ripwing")).toBeNull();
    adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed" });
    expect(adapter.getWork("tusksaw")?.employment).toBe("tamed");
  });
});
