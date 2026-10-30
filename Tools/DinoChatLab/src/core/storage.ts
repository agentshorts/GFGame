/**
 * Storage contract. `LocalStorageAdapter` is the lab default.
 * `SQLITE_SCHEMA` is the same shape a later Durable Object would execute.
 * This adapter stores no passwords, tokens, or API keys.
 */

import { applyStatEvent } from "./stats";
import {
  DEFAULT_REP_DELTAS,
  normalizeKnobs,
  type DecisionLogEntry,
  type DinoMemory,
  type MemoryKnobs,
  type PersonaSave,
  type PlayerStatsRow,
  type RelationshipMemory,
  type StatEvent,
  type StatEventDraft,
  type StatKind,
  type StoredPersona,
} from "./types";

export const STORAGE_PREFIX = "dcl:";
export const LOG_CAP = 500;
export const EVENT_CAP = 1000;

export interface KeyValueStore {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
  removeItem(key: string): void;
  key(index: number): string | null;
  readonly length: number;
}

export interface DinoStorage {
  listPersonas(): StoredPersona[];
  savePersona(input: PersonaSave): StoredPersona;
  deletePersona(id: string): void;
  getMemory(dinoId: string): DinoMemory | null;
  putMemory(memory: DinoMemory): void;
  deleteMemory(dinoId: string): void;
  getRelationship(dinoId: string, subjectKey: string): RelationshipMemory | null;
  putRelationship(memory: RelationshipMemory): void;
  listRelationships(dinoId: string): RelationshipMemory[];
  deleteRelationships(dinoId: string): void;
  appendLog(entry: DecisionLogEntry): DecisionLogEntry;
  listLog(dinoId?: string, limit?: number): DecisionLogEntry[];
  getStats(playerKey: string): PlayerStatsRow[];
  bumpStats(event: StatEventDraft, deltas?: Record<StatKind, number>): StatEvent;
  listEvents(playerKey?: string): StatEvent[];
  getKnobs(): MemoryKnobs;
  putKnobs(knobs: MemoryKnobs): void;
}

export class StorageConflictError extends Error {
  readonly status = 409;

  constructor(message = "someone else edited this, reload") {
    super(message);
    this.name = "StorageConflictError";
  }
}

export function isStorageConflict(error: unknown): error is StorageConflictError {
  return error instanceof StorageConflictError;
}

/** SQLite that matches the TypeScript rows. A later worker can run this as-is. */
export const SQLITE_SCHEMA = `
CREATE TABLE IF NOT EXISTS users (
  username TEXT PRIMARY KEY,
  created_at TEXT NOT NULL,
  last_seen TEXT NOT NULL,
  token_hash TEXT
);
CREATE TABLE IF NOT EXISTS personas (
  id TEXT PRIMARY KEY,
  owner TEXT NOT NULL,
  json TEXT NOT NULL,
  version INTEGER NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS memory (
  dino_id TEXT PRIMARY KEY,
  summary TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  turns_since_summary INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS relationships (
  dino_id TEXT NOT NULL,
  subject_key TEXT NOT NULL,
  kind TEXT NOT NULL,
  affinity INTEGER NOT NULL,
  summary TEXT NOT NULL,
  last_seen_at TEXT NOT NULL,
  interactions INTEGER NOT NULL,
  PRIMARY KEY (dino_id, subject_key)
);
CREATE TABLE IF NOT EXISTS player_stats (
  player_key TEXT NOT NULL,
  family_id TEXT NOT NULL,
  kills INTEGER NOT NULL DEFAULT 0,
  elder_kills INTEGER NOT NULL DEFAULT 0,
  hits_taken INTEGER NOT NULL DEFAULT 0,
  hits_dealt INTEGER NOT NULL DEFAULT 0,
  mango_given INTEGER NOT NULL DEFAULT 0,
  mango_refused INTEGER NOT NULL DEFAULT 0,
  blocks_given INTEGER NOT NULL DEFAULT 0,
  blocks_refused INTEGER NOT NULL DEFAULT 0,
  rentals_returned INTEGER NOT NULL DEFAULT 0,
  rentals_late INTEGER NOT NULL DEFAULT 0,
  rentals_downed INTEGER NOT NULL DEFAULT 0,
  bounties_claimed INTEGER NOT NULL DEFAULT 0,
  bounties_on_player INTEGER NOT NULL DEFAULT 0,
  rep INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (player_key, family_id)
);
CREATE TABLE IF NOT EXISTS stat_events (
  id INTEGER PRIMARY KEY,
  at TEXT NOT NULL,
  player_key TEXT NOT NULL,
  family_id TEXT NOT NULL,
  dino_id TEXT NOT NULL,
  kind TEXT NOT NULL,
  amount INTEGER NOT NULL,
  note TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS decision_log (
  id TEXT PRIMARY KEY,
  at TEXT NOT NULL,
  cycle_id TEXT NOT NULL,
  dino_id TEXT NOT NULL,
  subject_key TEXT NOT NULL,
  action TEXT NOT NULL,
  detail TEXT NOT NULL,
  metadata TEXT NOT NULL,
  engine TEXT NOT NULL,
  model_id TEXT,
  prompt_tokens INTEGER,
  completion_tokens INTEGER,
  duration_ms INTEGER
);
`.trim();

export function memoryKeyValueStore(): KeyValueStore {
  const map = new Map<string, string>();
  return {
    getItem(key) {
      return map.has(key) ? map.get(key) ?? null : null;
    },
    setItem(key, value) {
      map.set(key, String(value));
    },
    removeItem(key) {
      map.delete(key);
    },
    key(index) {
      return [...map.keys()][index] ?? null;
    },
    get length() {
      return map.size;
    },
  };
}

function parseJson<T>(raw: string | null): T | null {
  if (!raw) return null;
  try {
    return JSON.parse(raw) as T;
  } catch {
    return null;
  }
}

export class LocalStorageAdapter implements DinoStorage {
  constructor(
    private readonly store: KeyValueStore,
    private readonly prefix = STORAGE_PREFIX,
  ) {}

  private key(suffix: string): string {
    return `${this.prefix}${suffix}`;
  }

  private memoryKey(dinoId: string): string {
    return this.key(`memory:${encodeURIComponent(dinoId)}`);
  }

  private relKey(dinoId: string, subjectKey: string): string {
    return this.key(`rel:${encodeURIComponent(dinoId)}:${encodeURIComponent(subjectKey)}`);
  }

  private statsKey(playerKey: string): string {
    return this.key(`stats:${encodeURIComponent(playerKey)}`);
  }

  private keys(): string[] {
    const found: string[] = [];
    for (let index = 0; index < this.store.length; index += 1) {
      const key = this.store.key(index);
      if (key) found.push(key);
    }
    return found;
  }

  private nextSeq(): number {
    const current = Number(this.store.getItem(this.key("seq")) ?? "0");
    const next = Number.isFinite(current) ? Math.trunc(current) + 1 : 1;
    this.store.setItem(this.key("seq"), String(next));
    return next;
  }

  listPersonas(): StoredPersona[] {
    const rows = parseJson<StoredPersona[]>(this.store.getItem(this.key("personas")));
    if (!Array.isArray(rows)) return [];
    return rows.filter((row) => row && typeof row.id === "string");
  }

  savePersona(input: PersonaSave): StoredPersona {
    const all = this.listPersonas();
    const index = all.findIndex((row) => row.id === input.id);
    const now = input.updatedAt ?? new Date().toISOString();
    if (index === -1) {
      if (input.version !== 0) throw new StorageConflictError();
      const saved: StoredPersona = {
        id: input.id,
        owner: input.owner,
        version: 1,
        updatedAt: now,
        json: input.json,
      };
      all.push(saved);
      this.store.setItem(this.key("personas"), JSON.stringify(all));
      return saved;
    }
    const current = all[index];
    if (!current || current.version !== input.version) throw new StorageConflictError();
    const saved: StoredPersona = {
      id: input.id,
      owner: input.owner,
      version: current.version + 1,
      updatedAt: now,
      json: input.json,
    };
    all[index] = saved;
    this.store.setItem(this.key("personas"), JSON.stringify(all));
    return saved;
  }

  deletePersona(id: string): void {
    const all = this.listPersonas().filter((row) => row.id !== id);
    this.store.setItem(this.key("personas"), JSON.stringify(all));
    this.deleteMemory(id);
    this.deleteRelationships(id);
  }

  getMemory(dinoId: string): DinoMemory | null {
    return parseJson<DinoMemory>(this.store.getItem(this.memoryKey(dinoId)));
  }

  putMemory(memory: DinoMemory): void {
    this.store.setItem(this.memoryKey(memory.dinoId), JSON.stringify(memory));
  }

  deleteMemory(dinoId: string): void {
    this.store.removeItem(this.memoryKey(dinoId));
  }

  getRelationship(dinoId: string, subjectKey: string): RelationshipMemory | null {
    return parseJson<RelationshipMemory>(this.store.getItem(this.relKey(dinoId, subjectKey)));
  }

  putRelationship(memory: RelationshipMemory): void {
    this.store.setItem(this.relKey(memory.dinoId, memory.subjectKey), JSON.stringify(memory));
  }

  listRelationships(dinoId: string): RelationshipMemory[] {
    const prefix = this.key(`rel:${encodeURIComponent(dinoId)}:`);
    const rows: RelationshipMemory[] = [];
    for (const key of this.keys()) {
      if (!key.startsWith(prefix)) continue;
      const row = parseJson<RelationshipMemory>(this.store.getItem(key));
      if (row) rows.push(row);
    }
    return rows;
  }

  deleteRelationships(dinoId: string): void {
    const prefix = this.key(`rel:${encodeURIComponent(dinoId)}:`);
    for (const key of this.keys()) {
      if (key.startsWith(prefix)) this.store.removeItem(key);
    }
  }

  appendLog(entry: DecisionLogEntry): DecisionLogEntry {
    const saved: DecisionLogEntry = {
      ...entry,
      id: entry.id.trim().length > 0 ? entry.id : `log-${this.nextSeq()}`,
      metadata: entry.metadata ?? {},
    };
    const all = this.listLog();
    all.push(saved);
    const trimmed = all.slice(-LOG_CAP);
    this.store.setItem(this.key("log"), JSON.stringify(trimmed));
    return saved;
  }

  listLog(dinoId?: string, limit?: number): DecisionLogEntry[] {
    const rows = parseJson<DecisionLogEntry[]>(this.store.getItem(this.key("log"))) ?? [];
    const filtered = dinoId ? rows.filter((row) => row.dinoId === dinoId) : rows;
    if (limit === undefined) return filtered;
    const count = Math.max(0, Math.trunc(limit));
    return filtered.slice(-count);
  }

  getStats(playerKey: string): PlayerStatsRow[] {
    return parseJson<PlayerStatsRow[]>(this.store.getItem(this.statsKey(playerKey))) ?? [];
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
    const events = this.listEvents();
    events.push(saved);
    this.store.setItem(this.key("events"), JSON.stringify(events.slice(-EVENT_CAP)));
    const rows = applyStatEvent(this.getStats(saved.playerKey), saved, deltas);
    this.store.setItem(this.statsKey(saved.playerKey), JSON.stringify(rows));
    return saved;
  }

  listEvents(playerKey?: string): StatEvent[] {
    const rows = parseJson<StatEvent[]>(this.store.getItem(this.key("events"))) ?? [];
    return playerKey ? rows.filter((row) => row.playerKey === playerKey) : rows;
  }

  getKnobs(): MemoryKnobs {
    return normalizeKnobs(parseJson<unknown>(this.store.getItem(this.key("knobs"))) ?? {});
  }

  putKnobs(knobs: MemoryKnobs): void {
    this.store.setItem(this.key("knobs"), JSON.stringify(normalizeKnobs(knobs)));
  }
}

export function createDefaultStorage(): DinoStorage {
  if (typeof localStorage !== "undefined") return new LocalStorageAdapter(localStorage);
  return new LocalStorageAdapter(memoryKeyValueStore());
}
