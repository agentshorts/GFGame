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

export function blankWork(dinoId: string): DinoWork {
  return {
    dinoId,
    employerId: null,
    employment: null,
    wagesEarned: 0,
    mineralsFound: 0,
    seekingJob: false,
  };
}

export function normalizeWork(dinoId: string, value: unknown): DinoWork {
  const source = value && typeof value === "object" ? value as Partial<DinoWork> : {};
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
    wagesEarned: clampInt(source.wagesEarned, 0, 0, 1_000_000_000),
    mineralsFound: clampInt(source.mineralsFound, 0, 0, 1_000_000_000),
    seekingJob: source.seekingJob === true,
  };
}

export interface MateRankInput {
  wagesEarned: number;
  /** Caller-supplied labor or combat score, in points. */
  statScore: number;
  hp: number;
  maxHp: number;
}

/**
 * 0..10. Wages contribute `wagesEarned / 50`, HP contributes up to 20 when maxHp is set,
 * then the total is divided by 10. Minerals are not part of the rank.
 */
export function mateRank(input: MateRankInput): number {
  const wages = Math.max(0, input.wagesEarned) / 50;
  const stats = Math.max(0, input.statScore);
  const hp = input.maxHp > 0
    ? (Math.max(0, Math.min(input.hp, input.maxHp)) / input.maxHp) * 20
    : 0;
  return Math.max(0, Math.min(10, Math.floor((wages + stats + hp) / 10)));
}

/** True only when the dino wants work, has no employer, and the player is in its territory. */
export function seekingWorkInTerritory(work: DinoWork, inOwnTerritory: boolean): boolean {
  return work.seekingJob && work.employerId === null && inOwnTerritory;
}
