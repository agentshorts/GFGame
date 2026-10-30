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
  overBudgetFraction,
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
  creditWork,
  mateRank,
  normalizeWork,
  orderByMateRank,
  pickPartner,
  seekingWorkInTerritory,
  statScore,
  statsDigest,
  WageCreditError,
} from "../src/core/stats";
import {
  EVENT_CAP,
  LOG_CAP,
  LocalStorageAdapter,
  SQLITE_SCHEMA,
  StorageConflictError,
  bindStorage,
  creditWages,
  getMemory,
  getStats,
  getWork,
  memoryKeyValueStore,
  putMemory,
} from "../src/core/storage";
import {
  CORE_CONTRACT,
  DEFAULT_CONTEXT_BUDGET,
  REP_BAND_THRESHOLDS,
  defaultKnobs,
  estimateTokens,
  repBand,
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

  it("scales the blocks down when the budget is smaller than the caps", () => {
    const split = budgetSplit(128, 80, true);
    expect(split.total).toBe(128);
    expect(split.replyReserve).toBe(80);
    expect(split.persona).toBe(26);
    expect(split.world).toBe(4);
    expect(split.dinoSummary).toBe(7);
    expect(split.relationship).toBe(4);
    expect(split.elderStats).toBe(5);
    expect(split.recentTurns).toBe(2);
    expect(split.persona).toBeLessThan(900);
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

  it("summarizes on the 6th turn and past 80% of the budget", () => {
    const memory = { dinoId: "tusksaw", summary: "", updatedAt: "", turnsSinceSummary: 5 };
    expect(defaultKnobs().summarizeEvery).toBe(6);
    expect(defaultKnobs().summarizeAtFraction).toBe(0.8);
    expect(shouldSummarize(memory)).toBe(false);
    expect(shouldSummarize({ ...memory, turnsSinceSummary: 6 })).toBe(true);
    expect(overBudgetFraction(800, 1000)).toBe(false);
    expect(overBudgetFraction(799, 1000)).toBe(false);
    expect(overBudgetFraction(801, 1000)).toBe(true);
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
    expect(SQLITE_SCHEMA).toContain("lifetime_earnings");
    expect(SQLITE_SCHEMA).toContain("hired_at");
    expect(SQLITE_SCHEMA).not.toContain("wages_earned");
    expect(SQLITE_SCHEMA).not.toContain("mate_rank");
    expect(CORE_CONTRACT).toBe("dino-chat-memory/2");
  });

  it("drops decision-log and stat-event rows past the caps", () => {
    expect(LOG_CAP).toBe(500);
    expect(EVENT_CAP).toBe(1000);
    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    for (let index = 0; index < LOG_CAP + 1; index += 1) {
      adapter.appendLog({
        id: `log-${index}`,
        at: "t",
        cycleId: "cycle-1",
        dinoId: "tusksaw",
        subjectKey: "pip",
        action: "reply",
        detail: "cap",
        metadata: {},
        engine: "eliza",
        modelId: null,
        promptTokens: null,
        completionTokens: null,
        durationMs: 1,
      });
    }
    const log = adapter.listLog();
    expect(log).toHaveLength(LOG_CAP);
    expect(log[0]?.id).toBe("log-1");
    expect(log.at(-1)?.id).toBe(`log-${LOG_CAP}`);

    for (let index = 0; index < EVENT_CAP + 1; index += 1) {
      adapter.bumpStats(event({ kind: "hit_taken", note: `e-${index}` }));
    }
    const events = adapter.listEvents();
    expect(events).toHaveLength(EVENT_CAP);
    expect(events[0]?.note).toBe("e-1");
    expect(events.at(-1)?.note).toBe(`e-${EVENT_CAP}`);
  });

  it("publishes the rep band edges and fails if those edges move", () => {
    expect(REP_BAND_THRESHOLDS).toEqual({ friendlyAt: 20, waryAt: -8, hostileAt: -40 });
    expect(repBand(20)).toBe("friendly");
    expect(repBand(19)).toBe("neutral");
    expect(repBand(-7)).toBe("neutral");
    expect(repBand(-8)).toBe("wary");
    expect(repBand(-39)).toBe("wary");
    expect(repBand(-40)).toBe("hostile");
    expect(repBand(100)).toBe("friendly");
    expect(repBand(-100)).toBe("hostile");
    expect(Object.isFrozen(REP_BAND_THRESHOLDS)).toBe(true);
  });

  it("keeps a factory-work hook and ranks by lifetime earnings", () => {
    const early = "2020-01-01T00:00:00.000Z";
    const late = "2024-06-01T00:00:00.000Z";
    const seeking = {
      ...blankWork("tusksaw"),
      seekingJob: true,
      lifetimeEarnings: 0,
      mineralsFound: 4,
    };
    expect(seekingWorkInTerritory(seeking, true)).toBe(true);
    expect(seekingWorkInTerritory(seeking, false)).toBe(false);
    expect(seekingWorkInTerritory({
      ...seeking,
      employerId: "kiln",
      employment: "rented",
      hiredAt: early,
    }, true)).toBe(false);

    expect(normalizeWork("tusksaw", { lifetimeEarnings: -20, mineralsFound: -3 }).lifetimeEarnings).toBe(0);
    expect(normalizeWork("tusksaw", { mineralsFound: -3 }).mineralsFound).toBe(0);
    expect(normalizeWork("tusksaw", { wagesEarned: 12 }).lifetimeEarnings).toBe(12);

    const richerLater = {
      ...blankWork("rich"),
      lifetimeEarnings: 50,
      hiredAt: late,
      hp: 1,
      statScore: 0,
    };
    const poorerEarlier = {
      ...blankWork("poor"),
      lifetimeEarnings: 40,
      hiredAt: early,
      hp: 999,
      statScore: 80,
    };
    // 40 and 50 collapse together under a divisor of 40, and the old `/ 50` blend
    // then lets the earlier hire outrank the richer dino. Raw earnings must not.
    expect(orderByMateRank([poorerEarlier, richerLater]).map((row) => row.dinoId)).toEqual(["rich", "poor"]);
    expect(mateRank([poorerEarlier, richerLater], "rich")).toBe(1);
    expect(mateRank([poorerEarlier, richerLater], "poor")).toBe(2);
    expect(mateRank([poorerEarlier, richerLater], "missing")).toBe(0);
    const fortyNine = { ...blankWork("forty-nine"), lifetimeEarnings: 49, hiredAt: late };
    const one = { ...blankWork("one"), lifetimeEarnings: 1, hiredAt: early };
    expect(mateRank([one, fortyNine], "forty-nine")).toBe(1);
    expect(mateRank([one, fortyNine], "one")).toBe(2);

    const tiedEarly = { ...blankWork("early"), lifetimeEarnings: 80, hiredAt: early, hp: 1 };
    const tiedLate = { ...blankWork("late"), lifetimeEarnings: 80, hiredAt: late, hp: 999 };
    expect(mateRank([tiedLate, tiedEarly], "early")).toBe(1);
    expect(mateRank([tiedLate, tiedEarly], "late")).toBe(2);
    const undated = { ...blankWork("undated"), lifetimeEarnings: 80, hiredAt: null };
    expect(mateRank([undated, tiedEarly], "early")).toBe(1);
    expect(mateRank([tiedEarly, undated], "undated")).toBe(2);

    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    bindStorage(adapter);
    adapter.putWork(seeking);
    expect(adapter.getWork("tusksaw")).toMatchObject({
      lifetimeEarnings: 0,
      mineralsFound: 4,
      seekingJob: true,
      hiredAt: null,
    });
    expect(adapter.getWork("ripwing")).toBeNull();
    adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: late, lifetimeEarnings: 50 });
    expect(adapter.getWork("tusksaw")?.employment).toBe("tamed");
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(50);

    adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: late, lifetimeEarnings: 20 });
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(50);
    expect(mateRank([adapter.getWork("tusksaw")!, poorerEarlier], "tusksaw")).toBe(1);
    adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: "2026-01-01T00:00:00.000Z", lifetimeEarnings: 50 });
    expect(adapter.getWork("tusksaw")?.hiredAt).toBe(late);
    adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: early, lifetimeEarnings: 50 });
    expect(adapter.getWork("tusksaw")?.hiredAt).toBe(early);

    expect(() => adapter.creditWages("tusksaw", -1)).toThrow(WageCreditError);
    expect(() => creditWages("tusksaw", Number.NaN)).toThrow(WageCreditError);
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(50);
    expect(creditWork(adapter.getWork("tusksaw")!, 0).lifetimeEarnings).toBe(50);
    expect(adapter.creditWages("tusksaw", 2.9).lifetimeEarnings).toBe(52);

    const leader = { ...blankWork("leader"), lifetimeEarnings: 400, hiredAt: late };
    const trailer = { ...blankWork("trailer"), lifetimeEarnings: 10, hiredAt: early };
    const pool = () => [adapter.getWork("tusksaw")!, leader, trailer];
    expect(mateRank(pool(), "tusksaw")).toBe(2);
    creditWages("tusksaw", 100);
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(152);
    expect(mateRank(pool(), "tusksaw")).toBe(2);
    creditWages("tusksaw", 300);
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(452);
    expect(mateRank(pool(), "tusksaw")).toBe(1);

    expect(getWork("tusksaw")?.lifetimeEarnings).toBe(452);
    adapter.bumpStats(event({ kind: "mango_given" }));
    expect(getStats("pip").find((row) => row.familyId === "pack")?.mangoGiven).toBe(1);
    putMemory({ dinoId: "tusksaw", summary: "paid in moonstone", updatedAt: early, turnsSinceSummary: 1 });
    expect(getMemory("tusksaw")?.summary).toBe("paid in moonstone");

    adapter.savePersona({ id: "tusksaw", owner: "local", version: 0, json: "{}" });
    adapter.deletePersona("tusksaw");
    expect(adapter.getWork("tusksaw")).toBeNull();
    expect(adapter.getMemory("tusksaw")).toBeNull();
  });

  it("picks a partner by stats, then HP, then age", () => {
    const chooser = { dinoId: "chooser", sex: "female" as const };
    const bruised = {
      dinoId: "bruised",
      sex: "male" as const,
      maxHp: 40,
      strength: 10,
      speed: 10,
      hp: 1,
      age: 2,
    };
    const healthy = {
      dinoId: "healthy",
      sex: "male" as const,
      maxHp: 10,
      strength: 10,
      speed: 10,
      hp: 100,
      age: 9,
    };
    expect(statScore(bruised)).toBe(60);
    expect(statScore(healthy)).toBe(30);
    expect(pickPartner(chooser, [healthy, bruised])?.dinoId).toBe("bruised");

    const young = { ...healthy, dinoId: "young", hp: 10, age: 1 };
    const old = { ...healthy, dinoId: "old", hp: 10, age: 8 };
    expect(pickPartner(chooser, [young, old])?.dinoId).toBe("old");

    const lowHp = { ...healthy, dinoId: "low", hp: 3, age: 8 };
    const highHp = { ...healthy, dinoId: "high", hp: 9, age: 1 };
    expect(pickPartner(chooser, [lowHp, highHp])?.dinoId).toBe("high");
    expect(pickPartner(chooser, [{ ...bruised, sex: "female" }])).toBeNull();
  });
});
