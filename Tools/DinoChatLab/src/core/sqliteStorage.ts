/**
 * Browser SQLite adapter for the memory core.
 *
 * sql.js 1.x (MIT) loads SQLite compiled to WebAssembly. SQLite itself is
 * public domain. wa-sqlite is the other permissive WASM build; sql.js is the
 * one Vitest can open in Node without an OPFS worker.
 *
 * The database runs `SQLITE_SCHEMA` unchanged, including `behavior_log`, so a
 * later Durable Object can execute that same script. `lab_kv` is only the
 * id counter and the knobs blob. This file never writes `users.token_hash`
 * and it stores no passwords, tokens, or keys.
 *
 * Bytes are written to OPFS when the origin allows it, otherwise IndexedDB.
 */

import {
  BEHAVIOR_CAP,
  EVENT_CAP,
  LOG_CAP,
  LocalStorageAdapter,
  SQLITE_SCHEMA,
  StorageConflictError,
  behaviorPayload,
  tailRows,
  type DinoStorage,
  type KeyValueStore,
} from "./storage";
import { applyStatEvent, blankWork, creditWork, mergeWork, normalizeWork } from "./stats";
import {
  DEFAULT_REP_DELTAS,
  normalizeKnobs,
  type BehaviorLogDraft,
  type BehaviorLogEntry,
  type DecisionLogEntry,
  type DinoMemory,
  type DinoWork,
  type MemoryKnobs,
  type PersonaSave,
  type PlayerStatsRow,
  type RelationshipMemory,
  type StatEvent,
  type StatEventDraft,
  type StatKind,
  type StoredPersona,
} from "./types";
import type { Database, SqlJsStatic, SqlValue } from "sql.js";

/** Bookkeeping for the lab adapter. Not part of the Durable Object script. */
const LAB_KV_SCHEMA = `
CREATE TABLE IF NOT EXISTS lab_kv (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
`.trim();

export const BEHAVIOR_LOG_EXPORT_KIND = "dino-behavior-log";
export const BEHAVIOR_LOG_EXPORT_VERSION = 1;

/** Preference stored beside the notebook, never inside the SQLite file. */
export const STORAGE_BACKEND_KEY = "dcl:storage-backend";
/** Set after the one-time copy so a later toggle does not overwrite SQLite. */
export const SQLITE_MIGRATED_KEY = "dcl:sqlite-migrated";

export type StorageBackend = "local" | "sqlite";
export type SqlitePlace = "opfs" | "indexeddb";

export interface BehaviorLogFile {
  kind: typeof BEHAVIOR_LOG_EXPORT_KIND;
  version: typeof BEHAVIOR_LOG_EXPORT_VERSION;
  exportedAt: string;
  entries: BehaviorLogEntry[];
}

export interface SqlitePersist {
  load(): Promise<Uint8Array | null>;
  save(bytes: Uint8Array): Promise<void>;
}

type SqlParam = string | number | null;
type SqlRow = Record<string, string | number | null>;

export interface LocalSnapshot {
  personas: StoredPersona[];
  memories: DinoMemory[];
  relationships: RelationshipMemory[];
  log: DecisionLogEntry[];
  events: StatEvent[];
  stats: Array<{ playerKey: string; rows: PlayerStatsRow[] }>;
  knobs: MemoryKnobs;
  work: DinoWork[];
  behavior: BehaviorLogEntry[];
}

let sqlModule: Promise<SqlJsStatic> | null = null;

function asInit(imported: { default?: unknown }): (options: { wasmBinary: Uint8Array }) => Promise<SqlJsStatic> {
  const init = typeof imported.default === "function" ? imported.default : imported;
  return init as (options: { wasmBinary: Uint8Array }) => Promise<SqlJsStatic>;
}

/**
 * Node tests pass `sql-wasm.wasm` and load the Node build.
 * The browser loads `sql-wasm-browser.wasm` with the browser build.
 * `import.meta.env.VITEST` is false in the app bundle, so the Node file stays out.
 */
export function loadSqlJs(wasmBinary?: Uint8Array): Promise<SqlJsStatic> {
  if (!sqlModule) {
    sqlModule = import.meta.env.VITEST ? loadNodeSql(wasmBinary) : loadBrowserSql();
  }
  return sqlModule;
}

async function loadNodeSql(wasmBinary?: Uint8Array): Promise<SqlJsStatic> {
  if (!wasmBinary) throw new Error("SQLite tests need the sql.js wasm bytes.");
  const imported = await import("sql.js/dist/sql-wasm.js");
  return asInit(imported)({ wasmBinary });
}

async function loadBrowserSql(): Promise<SqlJsStatic> {
  const href = (await import("sql.js/dist/sql-wasm-browser.wasm?url")).default;
  const response = await fetch(href);
  if (!response.ok) throw new Error(`Could not load SQLite (${response.status}).`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  const imported = await import("sql.js/dist/sql-wasm-browser.js");
  return asInit(imported)({ wasmBinary: bytes });
}

export async function openSqliteStorage(options?: {
  wasmBinary?: Uint8Array;
  bytes?: Uint8Array | null;
  persist?: SqlitePersist | null;
  place?: SqlitePlace | "memory";
}): Promise<SqliteStorage> {
  const SQL = await loadSqlJs(options?.wasmBinary);
  const seeded = options?.bytes ?? (options?.persist ? await options.persist.load() : null);
  const db = seeded && seeded.byteLength > 0 ? new SQL.Database(seeded) : new SQL.Database();
  return new SqliteStorage(db, options?.persist ?? null, options?.place ?? "memory");
}

export function readStorageBackend(store: KeyValueStore): StorageBackend {
  return store.getItem(STORAGE_BACKEND_KEY) === "sqlite" ? "sqlite" : "local";
}

export function memoryBytesStore(initial: Uint8Array | null = null): SqlitePersist & { snapshot(): Uint8Array | null } {
  let bytes = initial ? initial.slice() : null;
  return {
    async load() {
      return bytes ? bytes.slice() : null;
    },
    async save(next) {
      bytes = next.slice();
    },
    snapshot() {
      return bytes ? bytes.slice() : null;
    },
  };
}

const IDB_NAME = "dino-chat-lab";
const IDB_STORE = "sqlite";
const IDB_KEY = "database";
const OPFS_FILE = "dino-chat-lab.sqlite";

function idbDone<T>(request: IDBRequest<T>): Promise<T> {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error ?? new Error("IndexedDB request failed."));
  });
}

export function indexedDbPersist(): SqlitePersist {
  function open(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(IDB_NAME, 1);
      request.onupgradeneeded = () => {
        const db = request.result;
        if (!db.objectStoreNames.contains(IDB_STORE)) db.createObjectStore(IDB_STORE);
      };
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error ?? new Error("IndexedDB failed to open."));
    });
  }

  return {
    async load() {
      const db = await open();
      try {
        const tx = db.transaction(IDB_STORE, "readonly");
        const value = await idbDone(tx.objectStore(IDB_STORE).get(IDB_KEY));
        if (value instanceof Uint8Array) return value;
        if (value instanceof ArrayBuffer) return new Uint8Array(value);
        return null;
      } finally {
        db.close();
      }
    },
    async save(bytes) {
      const db = await open();
      try {
        const tx = db.transaction(IDB_STORE, "readwrite");
        await idbDone(tx.objectStore(IDB_STORE).put(bytes.slice(), IDB_KEY));
      } finally {
        db.close();
      }
    },
  };
}

export function opfsPersist(filename = OPFS_FILE): SqlitePersist {
  async function root(): Promise<FileSystemDirectoryHandle> {
    return navigator.storage.getDirectory();
  }

  return {
    async load() {
      try {
        const handle = await (await root()).getFileHandle(filename);
        const file = await handle.getFile();
        return new Uint8Array(await file.arrayBuffer());
      } catch (error) {
        if (error instanceof DOMException && error.name === "NotFoundError") return null;
        throw error;
      }
    },
    async save(bytes) {
      const handle = await (await root()).getFileHandle(filename, { create: true });
      const writable = await handle.createWritable();
      await writable.write(bytes.slice());
      await writable.close();
    },
  };
}

export async function tryOpfsPersist(): Promise<SqlitePersist | null> {
  const storage = globalThis.navigator?.storage;
  if (!storage || typeof storage.getDirectory !== "function") return null;
  try {
    await storage.getDirectory();
  } catch {
    return null;
  }
  return opfsPersist();
}

export async function choosePersist(options: {
  openOpfs: () => Promise<SqlitePersist | null>;
  openIndexedDb: () => SqlitePersist;
}): Promise<{ persist: SqlitePersist; place: SqlitePlace }> {
  const opfs = await options.openOpfs();
  if (opfs) return { persist: opfs, place: "opfs" };
  return { persist: options.openIndexedDb(), place: "indexeddb" };
}

export function chooseBrowserPersist(): Promise<{ persist: SqlitePersist; place: SqlitePlace }> {
  return choosePersist({
    openOpfs: () => tryOpfsPersist(),
    openIndexedDb: () => indexedDbPersist(),
  });
}

function cell(value: SqlValue | undefined): string | number | null {
  if (typeof value === "bigint") return Number(value);
  if (typeof value === "string" || typeof value === "number" || value === null) return value;
  return null;
}

function cellNumber(value: string | number | null | undefined): number {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value === "string" && value.trim()) {
    const parsed = Number(value);
    if (Number.isFinite(parsed)) return parsed;
  }
  return 0;
}

function cellText(value: string | number | null | undefined): string {
  if (typeof value === "string") return value;
  if (typeof value === "number") return String(value);
  return "";
}

function cellNullableText(value: string | number | null | undefined): string | null {
  if (value === null || value === undefined || value === "") return null;
  return cellText(value);
}

function cellNullableNumber(value: string | number | null | undefined): number | null {
  if (value === null || value === undefined || value === "") return null;
  return cellNumber(value);
}

function jsonObject(raw: string): Record<string, unknown> {
  try {
    return behaviorPayload(JSON.parse(raw) as unknown);
  } catch {
    return {};
  }
}

export class SqliteStorage implements DinoStorage {
  private chain: Promise<void> = Promise.resolve();

  constructor(
    private readonly db: Database,
    private readonly persist: SqlitePersist | null,
    readonly place: SqlitePlace | "memory",
  ) {
    this.db.exec(SQLITE_SCHEMA);
    this.db.exec(LAB_KV_SCHEMA);
  }

  private touch(): void {
    if (!this.persist) return;
    const persist = this.persist;
    const save = () => persist.save(this.db.export().slice());
    this.chain = this.chain.then(save, save);
  }

  flush(): Promise<void> {
    return this.chain;
  }

  private run(sql: string, params: SqlParam[] = []): void {
    this.db.run(sql, params);
  }

  private all(sql: string, params: SqlParam[] = []): SqlRow[] {
    const stmt = this.db.prepare(sql);
    try {
      if (params.length > 0) stmt.bind(params);
      const rows: SqlRow[] = [];
      while (stmt.step()) {
        const object = stmt.getAsObject();
        const row: SqlRow = {};
        for (const [key, value] of Object.entries(object)) row[key] = cell(value);
        rows.push(row);
      }
      return rows;
    } finally {
      stmt.free();
    }
  }

  private one(sql: string, params: SqlParam[] = []): SqlRow | null {
    return this.all(sql, params)[0] ?? null;
  }

  private putKv(key: string, value: string): void {
    this.run("INSERT OR REPLACE INTO lab_kv (key, value) VALUES (?, ?)", [key, value]);
  }

  private seq(): number {
    return cellNumber(this.one("SELECT value FROM lab_kv WHERE key = 'seq'")?.value);
  }

  private nextSeq(): number {
    const next = this.seq() + 1;
    this.putKv("seq", String(next));
    return next;
  }

  private raiseSeq(id: number): void {
    if (id > this.seq()) this.putKv("seq", String(Math.trunc(id)));
  }

  private prune(table: "decision_log" | "stat_events" | "behavior_log", cap: number): void {
    this.run(
      `DELETE FROM ${table} WHERE rowid NOT IN (SELECT rowid FROM ${table} ORDER BY rowid DESC LIMIT ?)`,
      [cap],
    );
  }

  isEmpty(): boolean {
    const row = this.one(`
      SELECT
        (SELECT COUNT(*) FROM memory)
        + (SELECT COUNT(*) FROM relationships)
        + (SELECT COUNT(*) FROM personas)
        + (SELECT COUNT(*) FROM player_stats)
        + (SELECT COUNT(*) FROM stat_events)
        + (SELECT COUNT(*) FROM decision_log)
        + (SELECT COUNT(*) FROM dino_work)
        + (SELECT COUNT(*) FROM behavior_log) AS n
    `);
    return cellNumber(row?.n) === 0;
  }

  tables(): string[] {
    return this.all("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name")
      .map((row) => cellText(row.name));
  }

  listPersonas(): StoredPersona[] {
    return this.all("SELECT id, owner, json, version, updated_at FROM personas ORDER BY rowid ASC").map((row) => ({
      id: cellText(row.id),
      owner: cellText(row.owner),
      json: cellText(row.json),
      version: cellNumber(row.version),
      updatedAt: cellText(row.updated_at),
    }));
  }

  savePersona(input: PersonaSave): StoredPersona {
    const existing = this.one("SELECT version FROM personas WHERE id = ?", [input.id]);
    const now = input.updatedAt ?? new Date().toISOString();
    if (!existing) {
      if (input.version !== 0) throw new StorageConflictError();
      const saved: StoredPersona = {
        id: input.id,
        owner: input.owner,
        version: 1,
        updatedAt: now,
        json: input.json,
      };
      this.insertPersona(saved);
      this.touch();
      return saved;
    }
    const version = cellNumber(existing.version);
    if (version !== input.version) throw new StorageConflictError();
    const saved: StoredPersona = {
      id: input.id,
      owner: input.owner,
      version: version + 1,
      updatedAt: now,
      json: input.json,
    };
    this.run(
      "UPDATE personas SET owner = ?, json = ?, version = ?, updated_at = ? WHERE id = ?",
      [saved.owner, saved.json, saved.version, saved.updatedAt, saved.id],
    );
    this.touch();
    return saved;
  }

  private insertPersona(row: StoredPersona): void {
    this.run(
      "INSERT INTO personas (id, owner, json, version, updated_at) VALUES (?, ?, ?, ?, ?)",
      [row.id, row.owner, row.json, row.version, row.updatedAt],
    );
  }

  deletePersona(id: string): void {
    this.run("DELETE FROM personas WHERE id = ?", [id]);
    this.deleteMemory(id);
    this.deleteRelationships(id);
    this.deleteWork(id);
    this.touch();
  }

  getMemory(dinoId: string): DinoMemory | null {
    const row = this.one(
      "SELECT dino_id, summary, updated_at, turns_since_summary FROM memory WHERE dino_id = ?",
      [dinoId],
    );
    if (!row) return null;
    return {
      dinoId: cellText(row.dino_id),
      summary: cellText(row.summary),
      updatedAt: cellText(row.updated_at),
      turnsSinceSummary: cellNumber(row.turns_since_summary),
    };
  }

  putMemory(memory: DinoMemory): void {
    this.writeMemory(memory);
    this.touch();
  }

  private writeMemory(memory: DinoMemory): void {
    this.run("DELETE FROM memory WHERE dino_id = ?", [memory.dinoId]);
    this.run(
      "INSERT INTO memory (dino_id, summary, updated_at, turns_since_summary) VALUES (?, ?, ?, ?)",
      [memory.dinoId, memory.summary, memory.updatedAt, Math.trunc(memory.turnsSinceSummary) || 0],
    );
  }

  deleteMemory(dinoId: string): void {
    this.run("DELETE FROM memory WHERE dino_id = ?", [dinoId]);
    this.touch();
  }

  getRelationship(dinoId: string, subjectKey: string): RelationshipMemory | null {
    const row = this.one(
      `SELECT dino_id, subject_key, kind, affinity, summary, last_seen_at, interactions
       FROM relationships WHERE dino_id = ? AND subject_key = ?`,
      [dinoId, subjectKey],
    );
    return row ? this.relationshipFrom(row) : null;
  }

  private relationshipFrom(row: SqlRow): RelationshipMemory {
    return {
      dinoId: cellText(row.dino_id),
      subjectKey: cellText(row.subject_key),
      kind: cellText(row.kind) as RelationshipMemory["kind"],
      affinity: cellNumber(row.affinity),
      summary: cellText(row.summary),
      lastSeenAt: cellText(row.last_seen_at),
      interactions: cellNumber(row.interactions),
    };
  }

  putRelationship(memory: RelationshipMemory): void {
    this.writeRelationship(memory);
    this.touch();
  }

  private writeRelationship(memory: RelationshipMemory): void {
    this.run(
      "DELETE FROM relationships WHERE dino_id = ? AND subject_key = ?",
      [memory.dinoId, memory.subjectKey],
    );
    this.run(
      `INSERT INTO relationships
        (dino_id, subject_key, kind, affinity, summary, last_seen_at, interactions)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
      [
        memory.dinoId,
        memory.subjectKey,
        memory.kind,
        Math.trunc(memory.affinity) || 0,
        memory.summary,
        memory.lastSeenAt,
        Math.trunc(memory.interactions) || 0,
      ],
    );
  }

  listRelationships(dinoId: string): RelationshipMemory[] {
    return this.all(
      `SELECT dino_id, subject_key, kind, affinity, summary, last_seen_at, interactions
       FROM relationships WHERE dino_id = ? ORDER BY rowid ASC`,
      [dinoId],
    ).map((row) => this.relationshipFrom(row));
  }

  deleteRelationships(dinoId: string): void {
    this.run("DELETE FROM relationships WHERE dino_id = ?", [dinoId]);
    this.touch();
  }

  appendLog(entry: DecisionLogEntry): DecisionLogEntry {
    const saved: DecisionLogEntry = {
      ...entry,
      id: entry.id.trim().length > 0 ? entry.id : `log-${this.nextSeq()}`,
      metadata: entry.metadata ?? {},
    };
    this.insertLog(saved);
    this.prune("decision_log", LOG_CAP);
    this.touch();
    return saved;
  }

  private insertLog(entry: DecisionLogEntry): void {
    this.run(
      `INSERT INTO decision_log (
        id, at, cycle_id, dino_id, subject_key, action, detail, metadata, engine,
        model_id, prompt_tokens, completion_tokens, duration_ms
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
      [
        entry.id,
        entry.at,
        entry.cycleId,
        entry.dinoId,
        entry.subjectKey,
        entry.action,
        entry.detail,
        JSON.stringify(entry.metadata ?? {}),
        entry.engine,
        entry.modelId,
        entry.promptTokens,
        entry.completionTokens,
        entry.durationMs,
      ],
    );
  }

  private logFrom(row: SqlRow): DecisionLogEntry {
    return {
      id: cellText(row.id),
      at: cellText(row.at),
      cycleId: cellText(row.cycle_id),
      dinoId: cellText(row.dino_id),
      subjectKey: cellText(row.subject_key),
      action: cellText(row.action),
      detail: cellText(row.detail),
      metadata: jsonObject(cellText(row.metadata)),
      engine: cellText(row.engine),
      modelId: cellNullableText(row.model_id),
      promptTokens: cellNullableNumber(row.prompt_tokens),
      completionTokens: cellNullableNumber(row.completion_tokens),
      durationMs: cellNullableNumber(row.duration_ms),
    };
  }

  listLog(dinoId?: string, limit?: number): DecisionLogEntry[] {
    const rows = dinoId
      ? this.all("SELECT * FROM decision_log WHERE dino_id = ? ORDER BY rowid ASC", [dinoId])
      : this.all("SELECT * FROM decision_log ORDER BY rowid ASC");
    return tailRows(rows.map((row) => this.logFrom(row)), limit);
  }

  appendBehavior(entry: BehaviorLogDraft): BehaviorLogEntry {
    const id = entry.id && entry.id > 0 ? Math.trunc(entry.id) : this.nextSeq();
    this.raiseSeq(id);
    const saved: BehaviorLogEntry = {
      id,
      dinoId: entry.dinoId,
      at: entry.at,
      kind: entry.kind.trim() || "note",
      payload: behaviorPayload(entry.payload),
    };
    this.run("INSERT OR REPLACE INTO behavior_log (id, dino_id, at, kind, payload) VALUES (?, ?, ?, ?, ?)", [
      saved.id,
      saved.dinoId,
      saved.at,
      saved.kind,
      JSON.stringify(saved.payload),
    ]);
    this.prune("behavior_log", BEHAVIOR_CAP);
    this.touch();
    return saved;
  }

  listBehavior(dinoId?: string, limit?: number): BehaviorLogEntry[] {
    const rows = dinoId
      ? this.all("SELECT id, dino_id, at, kind, payload FROM behavior_log WHERE dino_id = ? ORDER BY rowid ASC", [dinoId])
      : this.all("SELECT id, dino_id, at, kind, payload FROM behavior_log ORDER BY rowid ASC");
    const mapped = rows.map((row) => ({
      id: cellNumber(row.id),
      dinoId: cellText(row.dino_id),
      at: cellText(row.at),
      kind: cellText(row.kind) || "note",
      payload: jsonObject(cellText(row.payload)),
    }));
    return tailRows(mapped, limit);
  }

  clearBehavior(): void {
    this.run("DELETE FROM behavior_log");
    this.touch();
  }

  getStats(playerKey: string): PlayerStatsRow[] {
    return this.all(
      `SELECT player_key, family_id, kills, elder_kills, hits_taken, hits_dealt,
              mango_given, mango_refused, blocks_given, blocks_refused,
              rentals_returned, rentals_late, rentals_downed,
              bounties_claimed, bounties_on_player, rep, updated_at
       FROM player_stats WHERE player_key = ? ORDER BY rowid ASC`,
      [playerKey],
    ).map((row) => this.statsFrom(row));
  }

  private statsFrom(row: SqlRow): PlayerStatsRow {
    return {
      playerKey: cellText(row.player_key),
      familyId: cellText(row.family_id),
      kills: cellNumber(row.kills),
      elderKills: cellNumber(row.elder_kills),
      hitsTaken: cellNumber(row.hits_taken),
      hitsDealt: cellNumber(row.hits_dealt),
      mangoGiven: cellNumber(row.mango_given),
      mangoRefused: cellNumber(row.mango_refused),
      blocksGiven: cellNumber(row.blocks_given),
      blocksRefused: cellNumber(row.blocks_refused),
      rentalsReturned: cellNumber(row.rentals_returned),
      rentalsLate: cellNumber(row.rentals_late),
      rentalsDowned: cellNumber(row.rentals_downed),
      bountiesClaimed: cellNumber(row.bounties_claimed),
      bountiesOnPlayer: cellNumber(row.bounties_on_player),
      rep: cellNumber(row.rep),
      updatedAt: cellText(row.updated_at),
    };
  }

  private writeStats(playerKey: string, rows: readonly PlayerStatsRow[]): void {
    this.run("DELETE FROM player_stats WHERE player_key = ?", [playerKey]);
    for (const row of rows) {
      this.run(
        `INSERT INTO player_stats (
          player_key, family_id, kills, elder_kills, hits_taken, hits_dealt,
          mango_given, mango_refused, blocks_given, blocks_refused,
          rentals_returned, rentals_late, rentals_downed,
          bounties_claimed, bounties_on_player, rep, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
        [
          row.playerKey,
          row.familyId,
          row.kills,
          row.elderKills,
          row.hitsTaken,
          row.hitsDealt,
          row.mangoGiven,
          row.mangoRefused,
          row.blocksGiven,
          row.blocksRefused,
          row.rentalsReturned,
          row.rentalsLate,
          row.rentalsDowned,
          row.bountiesClaimed,
          row.bountiesOnPlayer,
          row.rep,
          row.updatedAt,
        ],
      );
    }
  }

  bumpStats(event: StatEventDraft, deltas: Record<StatKind, number> = DEFAULT_REP_DELTAS): StatEvent {
    const saved: StatEvent = {
      id: this.nextSeq(),
      at: event.at,
      playerKey: event.playerKey,
      familyId: event.familyId,
      dinoId: event.dinoId,
      kind: event.kind,
      amount: Math.max(1, Math.trunc(event.amount ?? 1)),
      note: event.note ?? "",
    };
    this.insertEvent(saved);
    this.prune("stat_events", EVENT_CAP);
    this.writeStats(saved.playerKey, applyStatEvent(this.getStats(saved.playerKey), saved, deltas));
    this.touch();
    return saved;
  }

  private insertEvent(event: StatEvent): void {
    this.run(
      `INSERT INTO stat_events (id, at, player_key, family_id, dino_id, kind, amount, note)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
      [event.id, event.at, event.playerKey, event.familyId, event.dinoId, event.kind, event.amount, event.note],
    );
  }

  private eventFrom(row: SqlRow): StatEvent {
    return {
      id: cellNumber(row.id),
      at: cellText(row.at),
      playerKey: cellText(row.player_key),
      familyId: cellText(row.family_id),
      dinoId: cellText(row.dino_id),
      kind: cellText(row.kind) as StatKind,
      amount: cellNumber(row.amount),
      note: cellText(row.note),
    };
  }

  listEvents(playerKey?: string): StatEvent[] {
    const rows = playerKey
      ? this.all("SELECT * FROM stat_events WHERE player_key = ? ORDER BY rowid ASC", [playerKey])
      : this.all("SELECT * FROM stat_events ORDER BY rowid ASC");
    return rows.map((row) => this.eventFrom(row));
  }

  getKnobs(): MemoryKnobs {
    const raw = this.one("SELECT value FROM lab_kv WHERE key = 'knobs'")?.value;
    if (typeof raw !== "string" || !raw) return normalizeKnobs({});
    try {
      return normalizeKnobs(JSON.parse(raw) as unknown);
    } catch {
      return normalizeKnobs({});
    }
  }

  putKnobs(knobs: MemoryKnobs): void {
    this.putKv("knobs", JSON.stringify(normalizeKnobs(knobs)));
    this.touch();
  }

  getWork(dinoId: string): DinoWork | null {
    const row = this.one(
      `SELECT dino_id, employer_id, employment, lifetime_earnings, minerals_found, seeking_job, hired_at
       FROM dino_work WHERE dino_id = ?`,
      [dinoId],
    );
    if (!row) return null;
    return normalizeWork(dinoId, {
      employerId: cellNullableText(row.employer_id),
      employment: cellNullableText(row.employment),
      lifetimeEarnings: cellNumber(row.lifetime_earnings),
      mineralsFound: cellNumber(row.minerals_found),
      seekingJob: cellNumber(row.seeking_job) === 1,
      hiredAt: cellNullableText(row.hired_at),
    });
  }

  putWork(work: DinoWork): void {
    this.writeWork(mergeWork(this.getWork(work.dinoId), work));
    this.touch();
  }

  private writeWork(work: DinoWork): void {
    const row = normalizeWork(work.dinoId, work);
    this.run("DELETE FROM dino_work WHERE dino_id = ?", [row.dinoId]);
    this.run(
      `INSERT INTO dino_work (
        dino_id, employer_id, employment, lifetime_earnings, minerals_found, seeking_job, hired_at
      ) VALUES (?, ?, ?, ?, ?, ?, ?)`,
      [
        row.dinoId,
        row.employerId,
        row.employment,
        row.lifetimeEarnings,
        row.mineralsFound,
        row.seekingJob ? 1 : 0,
        row.hiredAt,
      ],
    );
  }

  creditWages(dinoId: string, amount: number): DinoWork {
    const current = this.getWork(dinoId) ?? blankWork(dinoId);
    const credited = creditWork(current, amount);
    this.putWork(credited);
    return this.getWork(dinoId) ?? credited;
  }

  deleteWork(dinoId: string): void {
    this.run("DELETE FROM dino_work WHERE dino_id = ?", [dinoId]);
    this.touch();
  }

  /** Copies a local snapshot as stored. Stat rows are not replayed through deltas. */
  ingest(snapshot: LocalSnapshot): void {
    this.run("BEGIN");
    try {
      for (const persona of snapshot.personas) this.insertPersona(persona);
      for (const memory of snapshot.memories) this.writeMemory(memory);
      for (const relationship of snapshot.relationships) this.writeRelationship(relationship);
      for (const entry of snapshot.log) this.insertLog(entry);
      this.prune("decision_log", LOG_CAP);
      for (const event of snapshot.events) {
        this.insertEvent(event);
        this.raiseSeq(event.id);
      }
      this.prune("stat_events", EVENT_CAP);
      for (const group of snapshot.stats) this.writeStats(group.playerKey, group.rows);
      this.putKv("knobs", JSON.stringify(normalizeKnobs(snapshot.knobs)));
      for (const work of snapshot.work) this.writeWork(work);
      for (const entry of snapshot.behavior) {
        this.raiseSeq(entry.id);
        this.run(
          "INSERT OR REPLACE INTO behavior_log (id, dino_id, at, kind, payload) VALUES (?, ?, ?, ?, ?)",
          [entry.id, entry.dinoId, entry.at, entry.kind, JSON.stringify(behaviorPayload(entry.payload))],
        );
      }
      this.prune("behavior_log", BEHAVIOR_CAP);
      let max = this.seq();
      for (const entry of snapshot.log) {
        const match = /^log-(\d+)$/.exec(entry.id);
        if (match) max = Math.max(max, Number(match[1]));
      }
      this.raiseSeq(max);
      this.run("COMMIT");
    } catch (error) {
      this.run("ROLLBACK");
      throw error;
    }
    this.touch();
  }
}

export function readLocalSnapshot(source: LocalStorageAdapter): LocalSnapshot {
  const dinoIds = new Set(source.knownDinoIds());
  for (const row of source.listLog()) dinoIds.add(row.dinoId);
  for (const row of source.listBehavior()) dinoIds.add(row.dinoId);
  for (const row of source.listEvents()) dinoIds.add(row.dinoId);
  const memories: DinoMemory[] = [];
  const relationships: RelationshipMemory[] = [];
  const work: DinoWork[] = [];
  for (const dinoId of dinoIds) {
    const memory = source.getMemory(dinoId);
    if (memory) memories.push(memory);
    relationships.push(...source.listRelationships(dinoId));
    const job = source.getWork(dinoId);
    if (job) work.push(job);
  }
  const playerKeys = new Set(source.knownPlayerKeys());
  for (const event of source.listEvents()) playerKeys.add(event.playerKey);
  return {
    personas: source.listPersonas(),
    memories,
    relationships,
    log: source.listLog(),
    events: source.listEvents(),
    stats: [...playerKeys].map((playerKey) => ({ playerKey, rows: source.getStats(playerKey) })),
    knobs: source.getKnobs(),
    work,
    behavior: source.listBehavior(),
  };
}

/**
 * Copies localStorage into an empty SQLite file once.
 * A second call keeps the SQLite rows, even if localStorage changed afterwards.
 */
export function migrateLocalStorageOnce(
  target: SqliteStorage,
  source: LocalStorageAdapter,
  flagStore: KeyValueStore,
): boolean {
  if (flagStore.getItem(SQLITE_MIGRATED_KEY) === "1") return false;
  if (!target.isEmpty()) {
    flagStore.setItem(SQLITE_MIGRATED_KEY, "1");
    return false;
  }
  target.ingest(readLocalSnapshot(source));
  flagStore.setItem(SQLITE_MIGRATED_KEY, "1");
  return true;
}

export function exportBehaviorLog(
  storage: Pick<DinoStorage, "listBehavior">,
  exportedAt: string,
): string {
  const file: BehaviorLogFile = {
    kind: BEHAVIOR_LOG_EXPORT_KIND,
    version: BEHAVIOR_LOG_EXPORT_VERSION,
    exportedAt,
    entries: storage.listBehavior(),
  };
  return JSON.stringify(file);
}

export function parseBehaviorLog(json: string): BehaviorLogFile {
  let parsed: unknown;
  try {
    parsed = JSON.parse(json) as unknown;
  } catch {
    throw new Error("Behavior log export is not JSON.");
  }
  if (!parsed || typeof parsed !== "object") throw new Error("Behavior log export is not an object.");
  const file = parsed as Partial<BehaviorLogFile>;
  if (file.kind !== BEHAVIOR_LOG_EXPORT_KIND || file.version !== BEHAVIOR_LOG_EXPORT_VERSION) {
    throw new Error("Behavior log export has the wrong kind or version.");
  }
  if (!Array.isArray(file.entries)) throw new Error("Behavior log export has no entries.");
  const entries: BehaviorLogEntry[] = file.entries.map((row, index) => {
    if (!row || typeof row !== "object") throw new Error(`Behavior row ${index} is missing.`);
    const entry = row as Partial<BehaviorLogEntry>;
    if (typeof entry.dinoId !== "string" || typeof entry.at !== "string" || typeof entry.kind !== "string") {
      throw new Error(`Behavior row ${index} is missing dino, time, or kind.`);
    }
    return {
      id: typeof entry.id === "number" && entry.id > 0 ? Math.trunc(entry.id) : index + 1,
      dinoId: entry.dinoId,
      at: entry.at,
      kind: entry.kind.trim() || "note",
      payload: behaviorPayload(entry.payload),
    };
  });
  return {
    kind: BEHAVIOR_LOG_EXPORT_KIND,
    version: BEHAVIOR_LOG_EXPORT_VERSION,
    exportedAt: typeof file.exportedAt === "string" ? file.exportedAt : "",
    entries,
  };
}

/** Replaces the behavior log. A file longer than the cap keeps the newest rows. */
export function importBehaviorLog(storage: DinoStorage, json: string): BehaviorLogEntry[] {
  const file = parseBehaviorLog(json);
  storage.clearBehavior();
  for (const entry of file.entries) {
    storage.appendBehavior({
      id: entry.id,
      dinoId: entry.dinoId,
      at: entry.at,
      kind: entry.kind,
      payload: entry.payload,
    });
  }
  return storage.listBehavior();
}
