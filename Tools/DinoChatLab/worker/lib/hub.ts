import { blankWork, creditWork, emptyStats, mergeWork, normalizeWork } from "../../src/core/stats";
import {
  clampRep,
  DEFAULT_REP_DELTAS,
  STAT_KINDS,
  type DecisionLogEntry,
  type DinoMemory,
  type DinoWork,
  type PlayerStatsRow,
  type RelationshipMemory,
  type StatEvent,
  type StatEventDraft,
  type StatKind,
  type StoredPersona,
} from "../../src/core/types";
import { normalizeManaged } from "../../src/manager/profile";
import { num, text, type Row, type Sql } from "./sql";

export const PERSONA_MAX_PER_USER = 50;
export const LOG_PER_MINUTE = 60;
export const LOG_KEEP = 500;
export const STALE_VERSION = "someone else edited this, reload";

const SCHEMAS = [
  `CREATE TABLE IF NOT EXISTS users (
    username TEXT PRIMARY KEY,
    token_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL,
    last_seen TEXT NOT NULL
  )`,
  `CREATE TABLE IF NOT EXISTS personas (
    id TEXT PRIMARY KEY,
    owner TEXT NOT NULL,
    json TEXT NOT NULL,
    version INTEGER NOT NULL,
    updated_at TEXT NOT NULL
  )`,
  `CREATE TABLE IF NOT EXISTS memory (
    owner TEXT NOT NULL,
    dino_id TEXT NOT NULL,
    summary TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    turns_since_summary INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (owner, dino_id)
  )`,
  `CREATE TABLE IF NOT EXISTS relationships (
    owner TEXT NOT NULL,
    dino_id TEXT NOT NULL,
    subject_key TEXT NOT NULL,
    kind TEXT NOT NULL,
    affinity INTEGER NOT NULL,
    summary TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    interactions INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (owner, dino_id, subject_key)
  )`,
  `CREATE TABLE IF NOT EXISTS player_stats (
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
  )`,
  `CREATE TABLE IF NOT EXISTS stat_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    at TEXT NOT NULL,
    player_key TEXT NOT NULL,
    family_id TEXT NOT NULL,
    dino_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    amount INTEGER NOT NULL,
    note TEXT NOT NULL
  )`,
  `CREATE TABLE IF NOT EXISTS decision_log (
    id TEXT PRIMARY KEY,
    at TEXT NOT NULL,
    cycle_id TEXT NOT NULL,
    dino_id TEXT NOT NULL,
    subject_key TEXT NOT NULL,
    owner TEXT NOT NULL,
    action TEXT NOT NULL,
    detail TEXT NOT NULL,
    metadata TEXT NOT NULL,
    engine TEXT,
    model_id TEXT,
    prompt_tokens INTEGER,
    completion_tokens INTEGER,
    duration_ms INTEGER
  )`,
  `CREATE TABLE IF NOT EXISTS rate_limits (
    username TEXT NOT NULL,
    bucket TEXT NOT NULL,
    window TEXT NOT NULL,
    count INTEGER NOT NULL,
    PRIMARY KEY (username, bucket, window)
  )`,
  `CREATE TABLE IF NOT EXISTS dino_work (
    dino_id TEXT PRIMARY KEY,
    employer_id TEXT,
    employment TEXT,
    lifetime_earnings INTEGER NOT NULL DEFAULT 0,
    minerals_found INTEGER NOT NULL DEFAULT 0,
    seeking_job INTEGER NOT NULL DEFAULT 0,
    hired_at TEXT
  )`,
];

/** SQL columns for the v1 player_stats table. Names stay put so existing rows still match. */
const SQL_STAT_COLUMN: Record<StatKind, string> = {
  kill: "kills",
  elder_kill: "elder_kills",
  hit_taken: "hits_taken",
  hit_dealt: "hits_dealt",
  mango_given: "mango_given",
  mango_refused: "mango_refused",
  block_given: "blocks_given",
  block_refused: "blocks_refused",
  rental_returned: "rentals_returned",
  rental_late: "rentals_late",
  rental_downed: "rentals_downed",
  bounty_claimed: "bounties_claimed",
  bounty_placed: "bounties_on_player",
};

export interface SavePersonaResult {
  ok: boolean;
  status: number;
  error?: string;
  persona?: StoredPersona;
}

export interface DeleteResult {
  ok: boolean;
  status: number;
  error?: string;
}

export interface LogWriteResult {
  ok: boolean;
  status: number;
  error?: string;
}

export function migrateHub(sql: Sql): void {
  for (const statement of SCHEMAS) sql.exec(statement);
}

function minuteWindow(iso: string): string {
  return iso.slice(0, 16);
}

export function takeRate(sql: Sql, username: string, bucket: string, window: string, cap: number): boolean {
  const rows = sql.exec(
    `INSERT INTO rate_limits (username, bucket, window, count) VALUES (?, ?, ?, 1)
     ON CONFLICT(username, bucket, window) DO UPDATE SET count = count + 1
     WHERE rate_limits.count < ?
     RETURNING count`,
    username,
    bucket,
    window,
    cap,
  );
  return rows.length > 0;
}

function personaFromRow(row: Row): StoredPersona {
  return {
    id: text(row, "id"),
    owner: text(row, "owner"),
    version: num(row, "version"),
    updatedAt: text(row, "updated_at"),
    json: text(row, "json"),
  };
}

function memoryFromRow(row: Row): DinoMemory {
  return {
    dinoId: text(row, "dino_id"),
    summary: text(row, "summary"),
    updatedAt: text(row, "updated_at"),
    turnsSinceSummary: num(row, "turns_since_summary"),
  };
}

function relationshipFromRow(row: Row): RelationshipMemory {
  return {
    dinoId: text(row, "dino_id"),
    subjectKey: text(row, "subject_key"),
    kind: text(row, "kind") as RelationshipMemory["kind"],
    affinity: num(row, "affinity"),
    summary: text(row, "summary"),
    lastSeenAt: text(row, "last_seen_at"),
    interactions: num(row, "interactions"),
  };
}

function statsFromRow(row: Row): PlayerStatsRow {
  return {
    playerKey: text(row, "player_key"),
    familyId: text(row, "family_id"),
    kills: num(row, "kills"),
    elderKills: num(row, "elder_kills"),
    hitsTaken: num(row, "hits_taken"),
    hitsDealt: num(row, "hits_dealt"),
    mangoGiven: num(row, "mango_given"),
    mangoRefused: num(row, "mango_refused"),
    blocksGiven: num(row, "blocks_given"),
    blocksRefused: num(row, "blocks_refused"),
    rentalsReturned: num(row, "rentals_returned"),
    rentalsLate: num(row, "rentals_late"),
    rentalsDowned: num(row, "rentals_downed"),
    bountiesClaimed: num(row, "bounties_claimed"),
    bountiesOnPlayer: num(row, "bounties_on_player"),
    rep: num(row, "rep"),
    updatedAt: text(row, "updated_at"),
  };
}

function logFromRow(row: Row): DecisionLogEntry {
  let metadata: Record<string, unknown> = {};
  try {
    const parsed = JSON.parse(text(row, "metadata")) as unknown;
    if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) metadata = parsed as Record<string, unknown>;
  } catch {
    metadata = {};
  }
  return {
    id: text(row, "id"),
    at: text(row, "at"),
    cycleId: text(row, "cycle_id"),
    dinoId: text(row, "dino_id"),
    subjectKey: text(row, "subject_key"),
    action: text(row, "action"),
    detail: text(row, "detail"),
    metadata,
    engine: text(row, "engine") || "none",
    modelId: text(row, "model_id") || null,
    promptTokens: row.prompt_tokens === null || row.prompt_tokens === undefined ? null : num(row, "prompt_tokens"),
    completionTokens: row.completion_tokens === null || row.completion_tokens === undefined ? null : num(row, "completion_tokens"),
    durationMs: row.duration_ms === null || row.duration_ms === undefined ? null : num(row, "duration_ms"),
  };
}

const DINO_ID = /^[a-z0-9-]{1,64}$/;
const SUBJECT = /^[^\u0000-\u001f]{1,40}$/;
const FAMILY = /^[A-Za-z0-9_*:.-]{1,40}$/;

export class HubStore {
  constructor(private readonly sql: Sql) {}

  findUser(username: string): boolean {
    return this.sql.exec(`SELECT username FROM users WHERE username = ?`, username).length > 0;
  }

  insertUser(username: string, tokenHash: string, now: string): { created: boolean } {
    const rows = this.sql.exec(
      `INSERT INTO users (username, token_hash, created_at, last_seen)
       VALUES (?, ?, ?, ?)
       ON CONFLICT(username) DO NOTHING
       RETURNING username`,
      username,
      tokenHash,
      now,
      now,
    );
    return { created: rows.length > 0 };
  }

  deleteUser(username: string): void {
    this.sql.exec(`DELETE FROM users WHERE username = ?`, username);
  }

  usernameForToken(tokenHash: string): string | null {
    const rows = this.sql.exec(`SELECT username FROM users WHERE token_hash = ?`, tokenHash);
    return rows[0] ? text(rows[0], "username") : null;
  }

  touch(username: string, now: string): void {
    this.sql.exec(`UPDATE users SET last_seen = ? WHERE username = ?`, now, username);
  }

  listPersonas(): StoredPersona[] {
    const rows = this.sql.exec(`SELECT id, owner, json, version, updated_at FROM personas ORDER BY updated_at DESC`);
    return rows.map(personaFromRow);
  }

  savePersona(owner: string, id: string, record: unknown, version: number, now: string): SavePersonaResult {
    if (!DINO_ID.test(id)) return { ok: false, status: 400, error: "Persona id is not valid." };
    const normalized = normalizeManaged(id, record);
    if (!normalized.ok) return { ok: false, status: 400, error: normalized.error };
    const existing = this.sql.exec(`SELECT owner, version FROM personas WHERE id = ?`, id);
    if (existing.length === 0) {
      if (version !== 0) return { ok: false, status: 409, error: STALE_VERSION };
      const count = num(this.sql.exec(`SELECT COUNT(*) AS n FROM personas WHERE owner = ?`, owner)[0], "n");
      if (count >= PERSONA_MAX_PER_USER) {
        return { ok: false, status: 409, error: "You can save up to 50 dinos." };
      }
      this.sql.exec(
        `INSERT INTO personas (id, owner, json, version, updated_at) VALUES (?, ?, ?, 1, ?)`,
        id,
        owner,
        normalized.json,
        now,
      );
      return {
        ok: true,
        status: 201,
        persona: { id, owner, version: 1, updatedAt: now, json: normalized.json },
      };
    }
    const row = existing[0]!;
    if (text(row, "owner") !== owner) return { ok: false, status: 403, error: "Only the owner can edit this dino." };
    if (num(row, "version") !== version) return { ok: false, status: 409, error: STALE_VERSION };
    const next = version + 1;
    const updated = this.sql.exec(
      `UPDATE personas SET json = ?, version = ?, updated_at = ?
       WHERE id = ? AND owner = ? AND version = ?
       RETURNING version`,
      normalized.json,
      next,
      now,
      id,
      owner,
      version,
    );
    if (updated.length === 0) return { ok: false, status: 409, error: STALE_VERSION };
    return {
      ok: true,
      status: 200,
      persona: { id, owner, version: next, updatedAt: now, json: normalized.json },
    };
  }

  deletePersona(owner: string, id: string): DeleteResult {
    const existing = this.sql.exec(`SELECT owner FROM personas WHERE id = ?`, id);
    if (existing.length === 0) return { ok: false, status: 404, error: "No dino with that id." };
    if (text(existing[0], "owner") !== owner) return { ok: false, status: 403, error: "Only the owner can delete this dino." };
    this.sql.exec(`DELETE FROM personas WHERE id = ? AND owner = ?`, id, owner);
    return { ok: true, status: 200 };
  }

  getMemory(owner: string, dinoId: string): DinoMemory | null {
    if (!DINO_ID.test(dinoId)) return null;
    const rows = this.sql.exec(
      `SELECT dino_id, summary, updated_at, turns_since_summary FROM memory WHERE owner = ? AND dino_id = ?`,
      owner,
      dinoId,
    );
    return rows[0] ? memoryFromRow(rows[0]) : null;
  }

  putMemory(owner: string, memory: DinoMemory): void {
    if (!DINO_ID.test(memory.dinoId)) throw new Error("Dino id is not valid.");
    const summary = memory.summary.slice(0, 4000);
    const turns = Math.max(0, Math.floor(memory.turnsSinceSummary) || 0);
    this.sql.exec(
      `INSERT INTO memory (owner, dino_id, summary, updated_at, turns_since_summary)
       VALUES (?, ?, ?, ?, ?)
       ON CONFLICT(owner, dino_id) DO UPDATE SET
         summary = excluded.summary,
         updated_at = excluded.updated_at,
         turns_since_summary = excluded.turns_since_summary`,
      owner,
      memory.dinoId,
      summary,
      memory.updatedAt,
      turns,
    );
  }

  getRelationship(owner: string, dinoId: string, subjectKey: string): RelationshipMemory | null {
    const rows = this.sql.exec(
      `SELECT dino_id, subject_key, kind, affinity, summary, last_seen_at, interactions
       FROM relationships WHERE owner = ? AND dino_id = ? AND subject_key = ?`,
      owner,
      dinoId,
      subjectKey,
    );
    return rows[0] ? relationshipFromRow(rows[0]) : null;
  }

  putRelationship(owner: string, rel: RelationshipMemory): void {
    if (!DINO_ID.test(rel.dinoId) || !SUBJECT.test(rel.subjectKey)) throw new Error("Relationship key is not valid.");
    const affinity = Math.max(-10, Math.min(10, Math.round(rel.affinity) || 0));
    this.sql.exec(
      `INSERT INTO relationships
         (owner, dino_id, subject_key, kind, affinity, summary, last_seen_at, interactions)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(owner, dino_id, subject_key) DO UPDATE SET
         kind = excluded.kind,
         affinity = excluded.affinity,
         summary = excluded.summary,
         last_seen_at = excluded.last_seen_at,
         interactions = excluded.interactions`,
      owner,
      rel.dinoId,
      rel.subjectKey,
      rel.kind,
      affinity,
      rel.summary.slice(0, 2000),
      rel.lastSeenAt,
      Math.max(0, Math.floor(rel.interactions) || 0),
    );
  }

  appendLog(owner: string, row: DecisionLogEntry, now: string): LogWriteResult {
    if (!takeRate(this.sql, owner, "decision_log", minuteWindow(now), LOG_PER_MINUTE)) {
      return { ok: false, status: 429, error: "Too many decision-log writes this minute." };
    }
    if (!DINO_ID.test(row.dinoId) || !/^[\w-]{8,80}$/.test(row.id)) {
      return { ok: false, status: 400, error: "Decision log row is not valid." };
    }
    const metadata = JSON.stringify(row.metadata ?? {});
    if (metadata.length > 8000 || row.detail.length > 500) {
      return { ok: false, status: 413, error: "Decision log row is too large." };
    }
    const inserted = this.sql.exec(
      `INSERT INTO decision_log
         (id, at, cycle_id, dino_id, subject_key, owner, action, detail, metadata, engine, model_id, prompt_tokens, completion_tokens, duration_ms)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(id) DO NOTHING
       RETURNING id`,
      row.id,
      row.at,
      row.cycleId.slice(0, 80),
      row.dinoId,
      row.subjectKey.slice(0, 40),
      owner,
      row.action.slice(0, 32),
      row.detail.slice(0, 500),
      metadata,
      row.engine,
      row.modelId,
      row.promptTokens,
      row.completionTokens,
      row.durationMs,
    );
    if (inserted.length === 0) return { ok: false, status: 409, error: "That log id already exists." };
    this.sql.exec(
      `DELETE FROM decision_log WHERE owner = ? AND id NOT IN (
         SELECT id FROM decision_log WHERE owner = ? ORDER BY at DESC LIMIT ?
       )`,
      owner,
      owner,
      LOG_KEEP,
    );
    return { ok: true, status: 201 };
  }

  tailLog(owner: string, dinoId: string, limit: number): DecisionLogEntry[] {
    const cap = Math.min(50, Math.max(1, Math.floor(limit) || 20));
    const rows = dinoId
      ? this.sql.exec(
        `SELECT * FROM decision_log WHERE owner = ? AND dino_id = ? ORDER BY at DESC LIMIT ?`,
        owner,
        dinoId,
        cap,
      )
      : this.sql.exec(
        `SELECT * FROM decision_log WHERE owner = ? ORDER BY at DESC LIMIT ?`,
        owner,
        cap,
      );
    return rows.map(logFromRow);
  }

  getStats(playerKey: string, familyId: string): PlayerStatsRow | null {
    const rows = this.sql.exec(
      `SELECT * FROM player_stats WHERE player_key = ? AND family_id = ?`,
      playerKey,
      familyId,
    );
    return rows[0] ? statsFromRow(rows[0]) : null;
  }

  listStats(playerKey: string): PlayerStatsRow[] {
    return this.sql.exec(
      `SELECT * FROM player_stats WHERE player_key = ? ORDER BY family_id`,
      playerKey,
    ).map(statsFromRow);
  }

  bumpStats(owner: string, event: StatEventDraft, now: string): { ok: true; event: StatEvent; stats: PlayerStatsRow } | { ok: false; status: number; error: string } {
    if (!takeRate(this.sql, owner, "stat_events", minuteWindow(now), LOG_PER_MINUTE)) {
      return { ok: false, status: 429, error: "Too many stat writes this minute." };
    }
    if (!STAT_KINDS.includes(event.kind) || !FAMILY.test(event.familyId) || !SUBJECT.test(event.playerKey)) {
      return { ok: false, status: 400, error: "Stat event is not valid." };
    }
    if (!DINO_ID.test(event.dinoId)) return { ok: false, status: 400, error: "Dino id is not valid." };
    const amount = Math.floor(event.amount ?? 1);
    if (!Number.isFinite(amount) || amount < 1 || amount > 100) {
      return { ok: false, status: 400, error: "Stat amount has to be from 1 to 100." };
    }
    const inserted = this.sql.exec(
      `INSERT INTO stat_events (at, player_key, family_id, dino_id, kind, amount, note)
       VALUES (?, ?, ?, ?, ?, ?, ?)
       RETURNING id`,
      event.at || now,
      event.playerKey,
      event.familyId,
      event.dinoId,
      event.kind,
      amount,
      (event.note ?? "").slice(0, 200),
    );
    const saved: StatEvent = {
      id: num(inserted[0], "id"),
      at: event.at || now,
      playerKey: event.playerKey,
      familyId: event.familyId,
      dinoId: event.dinoId,
      kind: event.kind,
      amount,
      note: (event.note ?? "").slice(0, 200),
    };
    const delta = DEFAULT_REP_DELTAS[event.kind] * amount;
    this.addStat(event.playerKey, event.familyId, event.kind, amount, delta, now);
    if (event.familyId !== "*") this.addStat(event.playerKey, "*", event.kind, amount, delta, now);
    const stats = this.getStats(event.playerKey, event.familyId) ?? emptyStats(event.playerKey, event.familyId, now);
    return { ok: true, event: saved, stats };
  }

  getWork(dinoId: string): DinoWork | null {
    if (!DINO_ID.test(dinoId)) return null;
    const rows = this.sql.exec(`SELECT * FROM dino_work WHERE dino_id = ?`, dinoId);
    const row = rows[0];
    if (!row) return null;
    const employer = text(row, "employer_id");
    const employment = text(row, "employment");
    const hired = text(row, "hired_at");
    return normalizeWork(dinoId, {
      dinoId,
      employerId: employer || null,
      employment: employment === "rented" || employment === "tamed" ? employment : null,
      lifetimeEarnings: num(row, "lifetime_earnings"),
      mineralsFound: num(row, "minerals_found"),
      seekingJob: num(row, "seeking_job") === 1,
      hiredAt: hired || null,
    });
  }

  putWork(work: DinoWork): void {
    const merged = mergeWork(this.getWork(work.dinoId), normalizeWork(work.dinoId, work));
    this.writeWork(merged);
  }

  creditWages(dinoId: string, amount: number): DinoWork {
    if (!DINO_ID.test(dinoId)) throw new Error("Dino id is not valid.");
    const credited = creditWork(this.getWork(dinoId) ?? blankWork(dinoId), amount);
    this.putWork(credited);
    return this.getWork(dinoId) ?? credited;
  }

  private writeWork(work: DinoWork): void {
    this.sql.exec(
      `INSERT INTO dino_work
         (dino_id, employer_id, employment, lifetime_earnings, minerals_found, seeking_job, hired_at)
       VALUES (?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(dino_id) DO UPDATE SET
         employer_id = excluded.employer_id,
         employment = excluded.employment,
         lifetime_earnings = excluded.lifetime_earnings,
         minerals_found = excluded.minerals_found,
         seeking_job = excluded.seeking_job,
         hired_at = excluded.hired_at`,
      work.dinoId,
      work.employerId,
      work.employment,
      work.lifetimeEarnings,
      work.mineralsFound,
      work.seekingJob ? 1 : 0,
      work.hiredAt,
    );
  }

  private addStat(playerKey: string, familyId: string, kind: StatKind, amount: number, repDelta: number, now: string): void {
    const column = SQL_STAT_COLUMN[kind];
    if (!column) throw new Error("Unknown stat column.");
    this.sql.exec(
      `INSERT INTO player_stats (player_key, family_id, updated_at) VALUES (?, ?, ?)
       ON CONFLICT(player_key, family_id) DO UPDATE SET updated_at = excluded.updated_at`,
      playerKey,
      familyId,
      now,
    );
    const current = this.getStats(playerKey, familyId)?.rep ?? 0;
    this.sql.exec(
      `UPDATE player_stats SET ${column} = ${column} + ?, rep = ?, updated_at = ? WHERE player_key = ? AND family_id = ?`,
      amount,
      clampRep(current + repDelta),
      now,
      playerKey,
      familyId,
    );
  }
}
