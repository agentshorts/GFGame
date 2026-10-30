/**
 * Per-player stats. Rows match `player_stats` / `stat_events` in `SQLITE_SCHEMA`.
 * Elders receive `statsDigest`. Everyone else gets the rep band only.
 */

import { clampSummary } from "./memory";
import {
  clampInt,
  clampRep,
  DEFAULT_AFFINITY_DELTAS,
  DEFAULT_REP_DELTAS,
  estimateTokens,
  STAT_FIELD,
  STAT_KINDS,
  TOTAL_FAMILY_ID,
  repBand,
  type DinoWork,
  type PlayerStatsRow,
  type RepBand,
  type RepBandEdges,
  type StatEvent,
  type StatKind,
} from "./types";

export function emptyStats(playerKey: string, familyId: string, at: string): PlayerStatsRow {
  return {
    playerKey,
    familyId,
    kills: 0,
    elderKills: 0,
    hitsTaken: 0,
    hitsDealt: 0,
    mangoGiven: 0,
    mangoRefused: 0,
    blocksGiven: 0,
    blocksRefused: 0,
    rentalsReturned: 0,
    rentalsLate: 0,
    rentalsDowned: 0,
    bountiesClaimed: 0,
    bountiesOnPlayer: 0,
    rep: 0,
    updatedAt: at,
  };
}

export function affinityDeltaFor(kind: StatKind, amount = 1): number {
  const times = Math.max(1, Math.trunc(amount));
  return DEFAULT_AFFINITY_DELTAS[kind] * times;
}

/** Same function as `affinityDeltaFor`. Kept under the name from the work issue. */
export function affinityDelta(kind: StatKind, amount = 1): number {
  return affinityDeltaFor(kind, amount);
}

function findRow(rows: readonly PlayerStatsRow[], playerKey: string, familyId: string): PlayerStatsRow | undefined {
  return rows.find((row) => row.playerKey === playerKey && row.familyId === familyId);
}

function addOnto(
  rows: readonly PlayerStatsRow[],
  event: StatEvent,
  familyId: string,
  deltas: Record<StatKind, number>,
): PlayerStatsRow[] {
  const next = rows.map((row) => ({ ...row }));
  let row = findRow(next, event.playerKey, familyId);
  if (!row) {
    row = emptyStats(event.playerKey, familyId, event.at);
    next.push(row);
  }
  const field = STAT_FIELD[event.kind];
  const current = row[field];
  if (typeof current === "number") {
    const value = Math.max(0, current + event.amount);
    (row as unknown as Record<string, number>)[field] = value;
  }
  const delta = (deltas[event.kind] ?? DEFAULT_REP_DELTAS[event.kind]) * event.amount;
  row.rep = clampRep(row.rep + delta);
  row.updatedAt = event.at;
  return next;
}

/**
 * Updates the family row and the "*" totals row.
 * An event already aimed at "*" updates totals only.
 * `elder_kill` does not also increment `kills`.
 */
export function applyStatEvent(
  rows: readonly PlayerStatsRow[],
  event: StatEvent,
  deltas: Record<StatKind, number> = DEFAULT_REP_DELTAS,
): PlayerStatsRow[] {
  const amount = Math.max(1, Math.trunc(event.amount));
  const clean: StatEvent = { ...event, amount };
  let next = addOnto(rows, clean, event.familyId, deltas);
  if (event.familyId !== TOTAL_FAMILY_ID) next = addOnto(next, clean, TOTAL_FAMILY_ID, deltas);
  return next;
}

export function rowFor(
  rows: readonly PlayerStatsRow[],
  playerKey: string,
  familyId: string,
): PlayerStatsRow {
  return findRow(rows, playerKey, familyId) ?? emptyStats(playerKey, familyId, "");
}

export function repCue(band: RepBand): string {
  return `rep_${band}`;
}

export function asksForPlayerRecord(text: string): boolean {
  return /\b(think of me|family think|who killed|my record|reputation|what do you know about me|what do you think)\b/i
    .test(text);
}

function digestLine(row: PlayerStatsRow, band: RepBand, scope: string): string {
  return [
    `kills ${scope}: ${row.kills} (${row.elderKills} elder)`,
    `hits taken from us: ${row.hitsTaken}`,
    `hits dealt: ${row.hitsDealt}`,
    `mango given ${row.mangoGiven} / refused ${row.mangoRefused}`,
    `blocks given ${row.blocksGiven} / refused ${row.blocksRefused}`,
    `rentals: ${row.rentalsReturned} returned, ${row.rentalsLate} late, ${row.rentalsDowned} downed`,
    `bounties: ${row.bountiesClaimed} claimed, ${row.bountiesOnPlayer} on them`,
    `rep with us: ${row.rep} (${band})`,
  ].join(" | ");
}

export function statsDigest(args: {
  rows: readonly PlayerStatsRow[];
  playerKey: string;
  familyId: string;
  elder: boolean;
  tokenCap?: number;
  bands?: RepBandEdges;
}): string {
  const cap = args.tokenCap ?? 200;
  const row = rowFor(args.rows, args.playerKey, args.familyId);
  const band = repBand(row.rep, args.bands);
  if (!args.elder) return clampSummary(repCue(band), cap);
  const scope = args.familyId === TOTAL_FAMILY_ID ? "in all" : "of our family";
  const text = digestLine(row, band, scope);
  const fitted = clampSummary(text, cap);
  if (estimateTokens(fitted) > cap) return clampSummary(fitted, cap);
  return fitted;
}

export function isStatKind(value: string): value is StatKind {
  return (STAT_KINDS as readonly string[]).includes(value);
}

const EARNINGS_CAP = 1_000_000_000;

export class WageCreditError extends Error {
  constructor(message = "lifetime earnings only go up") {
    super(message);
    this.name = "WageCreditError";
  }
}

export function blankWork(dinoId: string): DinoWork {
  return {
    dinoId,
    employerId: null,
    employment: null,
    lifetimeEarnings: 0,
    mineralsFound: 0,
    seekingJob: false,
    hiredAt: null,
  };
}

function cleanHiredAt(value: unknown): string | null {
  if (typeof value !== "string") return null;
  const trimmed = value.trim();
  if (!trimmed) return null;
  const parsed = Date.parse(trimmed);
  return Number.isFinite(parsed) ? trimmed : null;
}

/** Whole material-value points to add. Rejects negatives, NaN, and infinities. */
export function wageCredit(amount: number): number {
  if (typeof amount !== "number" || !Number.isFinite(amount) || amount < 0) {
    throw new WageCreditError();
  }
  return Math.trunc(amount);
}

export function normalizeWork(dinoId: string, value: unknown): DinoWork {
  const source = value && typeof value === "object" ? value as Partial<DinoWork> : {};
  const legacy = source as Partial<DinoWork> & { wagesEarned?: unknown };
  const rawEarnings = source.lifetimeEarnings !== undefined && source.lifetimeEarnings !== null
    ? source.lifetimeEarnings
    : legacy.wagesEarned;
  const employerId = typeof source.employerId === "string" && source.employerId.trim().length > 0
    ? source.employerId.trim()
    : null;
  const employment = employerId && (source.employment === "rented" || source.employment === "tamed")
    ? source.employment
    : null;
  return {
    dinoId,
    employerId,
    employment,
    lifetimeEarnings: clampInt(rawEarnings, 0, 0, EARNINGS_CAP),
    mineralsFound: clampInt(source.mineralsFound, 0, 0, EARNINGS_CAP),
    seekingJob: source.seekingJob === true,
    hiredAt: cleanHiredAt(source.hiredAt),
  };
}

function earlierHire(current: string | null, incoming: string | null): string | null {
  if (!current) return incoming;
  if (!incoming) return current;
  return Date.parse(current) <= Date.parse(incoming) ? current : incoming;
}

/** Keeps the higher earnings total and the earlier hire. Other fields come from `incoming`. */
export function mergeWork(previous: DinoWork | null, incoming: DinoWork): DinoWork {
  const next = normalizeWork(incoming.dinoId, incoming);
  if (!previous) return next;
  return {
    ...next,
    lifetimeEarnings: Math.max(previous.lifetimeEarnings, next.lifetimeEarnings),
    hiredAt: earlierHire(previous.hiredAt, next.hiredAt),
  };
}

/** Adds a non-negative credit. Does not touch minerals, employer, or hire time. */
export function creditWork(work: DinoWork, amount: number): DinoWork {
  const credit = wageCredit(amount);
  return {
    ...work,
    lifetimeEarnings: Math.min(EARNINGS_CAP, work.lifetimeEarnings + credit),
  };
}

export interface MateStanding {
  dinoId: string;
  lifetimeEarnings: number;
  hiredAt: string | null;
}

function standingEarnings(row: MateStanding): number {
  return Number.isFinite(row.lifetimeEarnings) ? Math.max(0, row.lifetimeEarnings) : 0;
}

function hireTime(hiredAt: string | null): number {
  if (!hiredAt) return Number.POSITIVE_INFINITY;
  const parsed = Date.parse(hiredAt);
  return Number.isFinite(parsed) ? parsed : Number.POSITIVE_INFINITY;
}

/**
 * First pick is index 0. Order is lifetime earnings, then earliest hire, then id.
 * Stats and HP are not arguments. A wage divisor is not applied: 40 and 50 stay different.
 * The caller passes the eligible dinos of one species in one area.
 */
export function orderByMateRank<T extends MateStanding>(pool: readonly T[]): T[] {
  return [...pool].sort((a, b) => {
    const earnings = standingEarnings(b) - standingEarnings(a);
    if (earnings !== 0) return earnings;
    const hire = hireTime(a.hiredAt) - hireTime(b.hiredAt);
    if (hire !== 0) return hire;
    return a.dinoId.localeCompare(b.dinoId);
  });
}

/**
 * Place in `pool`. 1 is first pick. 0 means `dinoId` is not in the pool.
 * Place moves only when earnings in the pool change. HP never changes it.
 * Against an unchanged rival set, crediting this dino never makes the place worse.
 */
export function mateRank(pool: readonly MateStanding[], dinoId: string): number {
  const index = orderByMateRank(pool).findIndex((row) => row.dinoId === dinoId);
  return index < 0 ? 0 : index + 1;
}

export interface MateStats {
  maxHp: number;
  strength: number;
  speed: number;
  /** Current HP. Tiebreak only. A bruise does not change mate rank. */
  hp: number;
  /** Higher is older. Last tiebreak. */
  age: number;
}

function points(value: number): number {
  return Number.isFinite(value) ? Math.max(0, value) : 0;
}

/** Placeholder from #98: MaxHP + Strength + Speed. DataTables will replace it. */
export function statScore(stats: Pick<MateStats, "maxHp" | "strength" | "speed">): number {
  return points(stats.maxHp) + points(stats.strength) + points(stats.speed);
}

export const MATE_SEXES = ["male", "female"] as const;

export type MateSex = (typeof MATE_SEXES)[number];

export interface PartnerOption extends MateStats {
  dinoId: string;
  sex: MateSex;
}

/**
 * Hook for #103. One chooser, one partner. Highest stat score, then higher HP, then older.
 * Earnings are not consulted. Opposite sex only. Null when nobody is eligible.
 * This is not a mating round and it does not write a child.
 */
export function pickPartner<T extends PartnerOption>(
  chooser: Pick<PartnerOption, "dinoId" | "sex">,
  pool: readonly T[],
): T | null {
  const options = pool.filter((row) => row.dinoId !== chooser.dinoId && row.sex !== chooser.sex);
  if (options.length === 0) return null;
  return [...options].sort((a, b) => {
    const score = statScore(b) - statScore(a);
    if (score !== 0) return score;
    const hp = points(b.hp) - points(a.hp);
    if (hp !== 0) return hp;
    const age = points(b.age) - points(a.age);
    if (age !== 0) return age;
    return a.dinoId.localeCompare(b.dinoId);
  })[0] ?? null;
}

/** True only when the dino wants work, has no employer, and the player is in its territory. */
export function seekingWorkInTerritory(work: DinoWork, inOwnTerritory: boolean): boolean {
  return work.seekingJob && work.employerId === null && inOwnTerritory;
}
