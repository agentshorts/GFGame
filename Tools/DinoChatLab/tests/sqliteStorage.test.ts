import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { blankRelationship } from "../src/core/memory";
import { blankWork, WageCreditError } from "../src/core/stats";
import {
  BEHAVIOR_CAP,
  EVENT_CAP,
  LOG_CAP,
  LocalStorageAdapter,
  SQLITE_SCHEMA,
  StorageConflictError,
  bindStorage,
  getMemory,
  getWork,
  memoryKeyValueStore,
  type DinoStorage,
} from "../src/core/storage";
import {
  choosePersist,
  exportBehaviorLog,
  importBehaviorLog,
  memoryBytesStore,
  migrateLocalStorageOnce,
  openSqliteStorage,
  parseBehaviorLog,
  type SqliteStorage,
} from "../src/core/sqliteStorage";
import { defaultKnobs, type DecisionLogEntry, type StatEvent } from "../src/core/types";

const require = createRequire(import.meta.url);
const wasmBinary = readFileSync(require.resolve("sql.js/dist/sql-wasm.wasm"));
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

function log(detail: string, dinoId = "tusksaw"): DecisionLogEntry {
  return {
    id: "",
    at: detail,
    cycleId: "cycle-1",
    dinoId,
    subjectKey: "pip",
    action: "reply",
    detail,
    metadata: { interest: 6 },
    engine: "eliza",
    modelId: null,
    promptTokens: null,
    completionTokens: null,
    durationMs: 3,
  };
}

async function openLocal(): Promise<DinoStorage> {
  return new LocalStorageAdapter(memoryKeyValueStore());
}

async function openSqlite(): Promise<SqliteStorage> {
  return openSqliteStorage({ wasmBinary });
}

async function assertContract(adapter: DinoStorage): Promise<void> {
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
  const updated = adapter.savePersona({
    id: "custom",
    owner: "local",
    version: 1,
    json: "{\"id\":\"custom\",\"name\":\"Custom\"}",
  });
  expect(updated.version).toBe(2);
  expect(() => adapter.savePersona({ id: "custom", owner: "local", version: 1, json: "{}" })).toThrow(StorageConflictError);
  try {
    adapter.savePersona({ id: "custom", owner: "local", version: 1, json: "{}" });
  } catch (error) {
    expect(error).toBeInstanceOf(StorageConflictError);
    expect((error as StorageConflictError).status).toBe(409);
  }

  const logged = adapter.appendLog(log("interest 6"));
  expect(logged.id.length).toBeGreaterThan(0);
  expect(adapter.listLog("tusksaw")).toHaveLength(1);
  expect(adapter.listLog("tusksaw", 0)).toEqual([]);
  expect(adapter.listLog("tusksaw", 1)[0]?.detail).toBe("interest 6");

  adapter.bumpStats(event({ kind: "mango_given" }));
  const stats = adapter.getStats("pip");
  expect(stats.find((row) => row.familyId === "pack")?.mangoGiven).toBe(1);
  expect(stats.find((row) => row.familyId === "*")?.rep).toBe(3);

  adapter.putKnobs({ ...knobs, allChatBudget: 2048, quota: { ...knobs.quota, restSeconds: 12 } });
  expect(adapter.getKnobs().allChatBudget).toBe(2048);
  expect(adapter.getKnobs().quota.restSeconds).toBe(12);

  const early = "2020-01-01T00:00:00.000Z";
  const late = "2024-06-01T00:00:00.000Z";
  const seeking = { ...blankWork("tusksaw"), seekingJob: true, mineralsFound: 4 };
  adapter.putWork(seeking);
  expect(adapter.getWork("tusksaw")).toMatchObject({
    lifetimeEarnings: 0,
    mineralsFound: 4,
    seekingJob: true,
    hiredAt: null,
  });
  expect(adapter.getWork("ripwing")).toBeNull();
  adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: late, lifetimeEarnings: 50 });
  expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(50);
  adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: late, lifetimeEarnings: 20 });
  expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(50);
  adapter.putWork({ ...seeking, employerId: "kiln", employment: "tamed", hiredAt: early, lifetimeEarnings: 50 });
  expect(adapter.getWork("tusksaw")?.hiredAt).toBe(early);
  expect(() => adapter.creditWages("tusksaw", -1)).toThrow(WageCreditError);
  expect(adapter.creditWages("tusksaw", 2.9).lifetimeEarnings).toBe(52);

  bindStorage(adapter);
  expect(getWork("tusksaw")?.lifetimeEarnings).toBe(52);
  expect(getMemory("tusksaw")?.summary).toBe("owes a mango");

  adapter.savePersona({ id: "tusksaw", owner: "local", version: 0, json: "{}" });
  adapter.deletePersona("tusksaw");
  expect(adapter.getWork("tusksaw")).toBeNull();
  expect(adapter.getMemory("tusksaw")).toBeNull();
  expect(adapter.listRelationships("tusksaw")).toEqual([]);
}

describe("storage contract", () => {
  it("passes on localStorage", async () => {
    await assertContract(await openLocal());
  });

  it("passes on sqlite", async () => {
    const adapter = await openSqlite();
    await assertContract(adapter);
    expect(adapter.tables().sort()).toEqual([
      "behavior_log",
      "decision_log",
      "dino_work",
      "lab_kv",
      "memory",
      "personas",
      "player_stats",
      "relationships",
      "stat_events",
      "users",
    ]);
    expect(SQLITE_SCHEMA).toContain("CREATE TABLE IF NOT EXISTS behavior_log");
    expect(SQLITE_SCHEMA).toContain("dino_id TEXT NOT NULL");
    expect(SQLITE_SCHEMA).toContain("payload TEXT NOT NULL");
    expect(SQLITE_SCHEMA).toContain("token_hash TEXT");
  });
});

describe("caps", () => {
  async function assertCaps(adapter: DinoStorage): Promise<void> {
    for (let index = 0; index < LOG_CAP + 3; index += 1) {
      adapter.appendLog(log(String(index), index % 2 === 0 ? "tusksaw" : "ripwing"));
    }
    const logs = adapter.listLog();
    expect(logs).toHaveLength(LOG_CAP);
    expect(logs[0]?.detail).toBe("3");
    expect(logs.at(-1)?.detail).toBe(String(LOG_CAP + 2));
    expect(adapter.listLog(undefined, 1)[0]?.detail).toBe(String(LOG_CAP + 2));

    for (let index = 0; index < EVENT_CAP + 2; index += 1) {
      adapter.bumpStats(event({ kind: "hit_taken", note: String(index) }));
    }
    const events = adapter.listEvents();
    expect(events).toHaveLength(EVENT_CAP);
    expect(events[0]?.note).toBe("2");
    expect(events.at(-1)?.note).toBe(String(EVENT_CAP + 1));

    for (let index = 0; index < BEHAVIOR_CAP + 3; index += 1) {
      adapter.appendBehavior({
        dinoId: index % 2 === 0 ? "tusksaw" : "ripwing",
        at: `t-${index}`,
        kind: String(index),
        payload: { index },
      });
    }
    const behavior = adapter.listBehavior();
    expect(behavior).toHaveLength(BEHAVIOR_CAP);
    expect(behavior[0]?.kind).toBe("3");
    expect(behavior.at(-1)?.kind).toBe(String(BEHAVIOR_CAP + 2));
    expect(behavior.at(-1)?.payload).toEqual({ index: BEHAVIOR_CAP + 2 });
    expect(adapter.listBehavior("tusksaw").every((row) => row.dinoId === "tusksaw")).toBe(true);
    expect(adapter.listBehavior("tusksaw", 1)).toHaveLength(1);
  }

  it("prunes localStorage", async () => {
    await assertCaps(await openLocal());
  });

  it("prunes sqlite", async () => {
    await assertCaps(await openSqlite());
  });
});

describe("migration and export", () => {
  it("copies memory, stats, and work once", async () => {
    const kv = memoryKeyValueStore();
    const local = new LocalStorageAdapter(kv);
    const early = "2020-01-01T00:00:00.000Z";
    local.putMemory({ dinoId: "tusksaw", summary: "owes a mango", updatedAt: early, turnsSinceSummary: 2 });
    local.putRelationship(blankRelationship("tusksaw", "pip", early, "rival"));
    local.putWork({
      ...blankWork("tusksaw"),
      employerId: "kiln",
      employment: "tamed",
      hiredAt: early,
      lifetimeEarnings: 40,
      mineralsFound: 2,
    });
    const custom = { ...knobs, repDeltas: { ...knobs.repDeltas, kill: -1 }, allChatBudget: 2048 };
    local.putKnobs(custom);
    local.bumpStats(event({ kind: "kill", at: early, note: "lab" }), custom.repDeltas);
    local.appendBehavior({ dinoId: "tusksaw", at: early, kind: "glare", payload: { teeth: 2 } });
    local.appendLog({ ...log("interest"), id: "log-9", at: early });
    const created = local.savePersona({ id: "custom", owner: "local", version: 0, json: "{\"id\":\"custom\"}" });
    local.savePersona({ id: "custom", owner: "local", version: created.version, json: "{\"name\":\"Custom\"}" });

    const sqlite = await openSqlite();
    expect(migrateLocalStorageOnce(sqlite, local, kv)).toBe(true);
    expect(sqlite.getMemory("tusksaw")).toEqual(local.getMemory("tusksaw"));
    expect(sqlite.getWork("tusksaw")).toEqual(local.getWork("tusksaw"));
    expect(sqlite.getStats("pip")).toEqual(local.getStats("pip"));
    expect(sqlite.getStats("pip").find((row) => row.familyId === "pack")?.rep).toBe(-1);
    expect(sqlite.listEvents()).toEqual(local.listEvents());
    expect(sqlite.getKnobs().repDeltas.kill).toBe(-1);
    expect(sqlite.getKnobs().allChatBudget).toBe(2048);
    expect(sqlite.getRelationship("tusksaw", "pip")?.kind).toBe("rival");
    expect(sqlite.listBehavior()).toEqual(local.listBehavior());
    expect(sqlite.listLog()).toEqual(local.listLog());
    expect(sqlite.listPersonas()).toEqual(local.listPersonas());

    local.putMemory({ dinoId: "tusksaw", summary: "changed after the copy", updatedAt: early, turnsSinceSummary: 9 });
    expect(migrateLocalStorageOnce(sqlite, local, kv)).toBe(false);
    expect(sqlite.getMemory("tusksaw")?.summary).toBe("owes a mango");
  });

  it.each([
    ["localStorage", openLocal],
    ["sqlite", openSqlite],
  ])("round-trips the behavior log on %s", async (_name, open) => {
    const adapter = await open();
    const at = "2026-10-31T00:00:00.000Z";
    adapter.appendBehavior({ dinoId: "tusksaw", at, kind: "sniff", payload: { blocks: 3, note: "mango" } });
    adapter.appendBehavior({ dinoId: "ripwing", at, kind: "circle", payload: { height: 12 } });
    const json = exportBehaviorLog(adapter, at);
    const copy = await open();
    expect(importBehaviorLog(copy, json)).toEqual(adapter.listBehavior());
    expect(exportBehaviorLog(copy, at)).toBe(json);
    expect(() => parseBehaviorLog("{")).toThrow(/JSON/);
    expect(() => parseBehaviorLog("{\"kind\":\"nope\",\"version\":1,\"entries\":[]}")).toThrow(/kind/);
  });

  it("reopens sqlite from the byte sink", async () => {
    const sink = memoryBytesStore();
    const first = await openSqliteStorage({ wasmBinary, persist: sink, place: "indexeddb" });
    first.putMemory({ dinoId: "tusksaw", summary: "paid in moonstone", updatedAt: "t", turnsSinceSummary: 1 });
    first.putWork({ ...blankWork("tusksaw"), lifetimeEarnings: 12, mineralsFound: 1, seekingJob: true });
    first.appendBehavior({ dinoId: "tusksaw", at: "t", kind: "pocket", payload: { gems: 1 } });
    await first.flush();
    expect(sink.snapshot()?.byteLength).toBeGreaterThan(0);
    const second = await openSqliteStorage({ wasmBinary, persist: sink });
    expect(second.getMemory("tusksaw")?.summary).toBe("paid in moonstone");
    expect(second.getWork("tusksaw")?.lifetimeEarnings).toBe(12);
    expect(second.listBehavior()).toEqual(first.listBehavior());
  });

  it("prefers OPFS and falls back to IndexedDB", async () => {
    const indexed = memoryBytesStore();
    const opfs = memoryBytesStore();
    const preferred = await choosePersist({
      openOpfs: async () => opfs,
      openIndexedDb: () => indexed,
    });
    expect(preferred.place).toBe("opfs");
    expect(preferred.persist).toBe(opfs);
    const fallback = await choosePersist({
      openOpfs: async () => null,
      openIndexedDb: () => indexed,
    });
    expect(fallback.place).toBe("indexeddb");
    expect(fallback.persist).toBe(indexed);
  });
});
