/**
 * Elder shop rental ledger. Decision #18.
 *
 * The player pays blocks up front and a cut of each mineral the dino harvests.
 * A rental lasts 15 minutes unless the terms say otherwise. If the dino goes
 * down it quits and runs off. It does not die. That elder's prices go up.
 * An on-time return with minerals raises rep. A plain on-time return does not.
 * A late return is its own outcome. This file does not post into the feed.
 */

import { clampInt, clampRep, estimateTokens } from "../core/types";
import { POST_TOKEN_CAP } from "./types";

const SHARE_SCALE = 10_000;
const MAX_PRICE_SCALE_BPS = 100_000;
const MINERAL_CAP = 1_000_000_000;
const HARVEST_CAP = 1_000_000;
const MAX_DURATION_MS = 7 * 24 * 60 * 60 * 1000;

export const DEFAULT_RENTAL_TERMS = {
  /** Blocks due before the dino leaves the shop. */
  upfrontBlocks: 20,
  /** Fraction of each harvested mineral owed to the elder. */
  mineralShare: 0.1,
  /** How long the player may keep the dino. */
  durationMs: 15 * 60 * 1000,
  /** Added to the shop's price scale when a rental is downed. 0.1 is +10%. */
  downedPriceBump: 0.1,
  /** Rep for an on-time return that brought minerals home. */
  mineralReturnRep: 1,
  /** Rep for an on-time return with an empty sack. */
  plainReturnRep: 0,
  /**
   * Rep for a late return. Decision #18 does not set this, so it stays 0
   * until someone tunes it. The late counter still moves.
   */
  lateRep: 0,
  /**
   * Rep for a downed rental. Decision #18 moves the price, not rep,
   * so this stays 0 until someone tunes it.
   */
  downedRep: 0,
} as const;

export interface RentalTerms {
  upfrontBlocks: number;
  mineralShare: number;
  durationMs: number;
  downedPriceBump: number;
  mineralReturnRep: number;
  plainReturnRep: number;
  lateRep: number;
  downedRep: number;
}

export interface MineralStack {
  id: string;
  count: number;
}

export type RentalStatus = "active" | "returned" | "plain" | "late" | "downed";

export type RentalStatKind = "rental_returned" | "rental_late" | "rental_downed";

export interface RentalRecord {
  id: string;
  playerId: string;
  dinoId: string;
  elderId: string;
  familyId: string;
  startedAt: string;
  dueAt: string;
  /** Blocks actually taken, after the shop's current price scale. */
  blocksCharged: number;
  /** Terms captured at open. Later ledger edits do not rewrite this rental. */
  terms: RentalTerms;
  minerals: readonly MineralStack[];
  status: RentalStatus;
  closedAt: string | null;
  ranOff: boolean;
}

export interface ElderShop {
  elderId: string;
  familyId: string;
  /** 10000 is list price. Each downed rental adds that rental's bump. */
  priceScaleBps: number;
  repByPlayer: Readonly<Record<string, number>>;
}

export interface RentalLedger {
  terms: RentalTerms;
  elders: readonly ElderShop[];
  rentals: readonly RentalRecord[];
}

export interface RentalQuote {
  elderId: string;
  listBlocks: number;
  upfrontBlocks: number;
  priceScaleBps: number;
  mineralShareBps: number;
  durationMs: number;
}

export interface OpenRentalInput {
  id: string;
  playerId: string;
  dinoId: string;
  elderId: string;
  familyId: string;
  at: string;
  blocks: number;
}

export interface CloseRentalInput {
  at: string;
  playerName?: string;
  dinoName?: string;
  elderName?: string;
}

export interface RentalSettlement {
  rental: RentalRecord;
  outcome: Exclude<RentalStatus, "active">;
  statKind: RentalStatKind;
  statAmount: 1;
  /**
   * The whole reputation change at this shop.
   * Do not also apply the memory core's DEFAULT_REP_DELTAS for `statKind`.
   * That table uses different numbers and would bill the player twice.
   */
  repDelta: number;
  rep: number;
  shareDue: readonly MineralStack[];
  priceBumpBps: number;
  priceScaleBps: number;
  quit: boolean;
  ranOff: boolean;
  /** A downed rental quits. The dino is still alive. */
  died: false;
  post: string | null;
}

export type RentalRefusal =
  | "bad-id"
  | "bad-time"
  | "duplicate-id"
  | "dino-busy"
  | "elder-family"
  | "bad-blocks"
  | "short-blocks"
  | "unknown-rental"
  | "not-active"
  | "bad-mineral"
  | "time-backwards";

export interface RentalOk<T> {
  ok: true;
  ledger: RentalLedger;
  value: T;
}

export interface RentalNo {
  ok: false;
  reason: RentalRefusal;
  detail: string;
}

export type RentalResult<T> = RentalOk<T> | RentalNo;

export interface OpenedRental {
  rental: RentalRecord;
  blocksCharged: number;
  blocksLeft: number;
  quote: RentalQuote;
}

function snapUnit(value: unknown, fallback: number): number {
  if (typeof value !== "number" || !Number.isFinite(value)) return fallback;
  const clamped = Math.min(1, Math.max(0, value));
  return Math.round(clamped * SHARE_SCALE) / SHARE_SCALE;
}

export function normalizeTerms(partial?: Partial<RentalTerms>): RentalTerms {
  const source = partial ?? {};
  return {
    upfrontBlocks: clampInt(source.upfrontBlocks, DEFAULT_RENTAL_TERMS.upfrontBlocks, 0, 100_000),
    mineralShare: snapUnit(source.mineralShare, DEFAULT_RENTAL_TERMS.mineralShare),
    durationMs: clampInt(source.durationMs, DEFAULT_RENTAL_TERMS.durationMs, 1, MAX_DURATION_MS),
    downedPriceBump: snapUnit(source.downedPriceBump, DEFAULT_RENTAL_TERMS.downedPriceBump),
    mineralReturnRep: clampInt(source.mineralReturnRep, DEFAULT_RENTAL_TERMS.mineralReturnRep, -100, 100),
    plainReturnRep: clampInt(source.plainReturnRep, DEFAULT_RENTAL_TERMS.plainReturnRep, -100, 100),
    lateRep: clampInt(source.lateRep, DEFAULT_RENTAL_TERMS.lateRep, -100, 100),
    downedRep: clampInt(source.downedRep, DEFAULT_RENTAL_TERMS.downedRep, -100, 100),
  };
}

function basisPoints(rate: number): number {
  return Math.round(rate * SHARE_SCALE);
}

function quotedBlocks(listBlocks: number, priceScaleBps: number): number {
  return Math.floor((listBlocks * priceScaleBps + SHARE_SCALE / 2) / SHARE_SCALE);
}

/** Whole minerals owed. The cut is floored per mineral, in basis points. */
export function owedMinerals(minerals: readonly MineralStack[], mineralShare: number): MineralStack[] {
  const shareBps = basisPoints(mineralShare);
  const due: MineralStack[] = [];
  for (const stack of minerals) {
    const count = Math.floor((stack.count * shareBps) / SHARE_SCALE);
    if (count > 0) due.push({ id: stack.id, count });
  }
  return due;
}

export function createLedger(partial?: Partial<RentalTerms>): RentalLedger {
  return { terms: normalizeTerms(partial), elders: [], rentals: [] };
}

export function withTerms(ledger: RentalLedger, partial: Partial<RentalTerms>): RentalLedger {
  return { ...ledger, terms: normalizeTerms({ ...ledger.terms, ...partial }) };
}

function no(reason: RentalRefusal, detail: string): RentalNo {
  return { ok: false, reason, detail };
}

function yes<T>(ledger: RentalLedger, value: T): RentalOk<T> {
  return { ok: true, ledger, value };
}

function cleanId(value: unknown): string | null {
  if (typeof value !== "string") return null;
  const trimmed = value.trim();
  if (!trimmed || trimmed.length > 64) return null;
  if (trimmed === "__proto__" || trimmed === "prototype" || trimmed === "constructor") return null;
  if (/[\u0000-\u001f]/.test(trimmed)) return null;
  return trimmed;
}

function parseTime(value: unknown): number | null {
  if (typeof value !== "string" || value.trim().length === 0) return null;
  const ms = Date.parse(value);
  return Number.isFinite(ms) ? ms : null;
}

function stamp(ms: number): string {
  return new Date(ms).toISOString();
}

export function elderOf(ledger: RentalLedger, elderId: string): ElderShop | null {
  return ledger.elders.find((shop) => shop.elderId === elderId) ?? null;
}

export function activeRentals(ledger: RentalLedger, playerId?: string): RentalRecord[] {
  return ledger.rentals.filter((rental) => {
    if (rental.status !== "active") return false;
    return playerId === undefined || rental.playerId === playerId;
  });
}

export function quoteRental(ledger: RentalLedger, elderId: string): RentalQuote {
  const shop = elderOf(ledger, elderId);
  const priceScaleBps = shop?.priceScaleBps ?? SHARE_SCALE;
  const listBlocks = ledger.terms.upfrontBlocks;
  return {
    elderId,
    listBlocks,
    upfrontBlocks: quotedBlocks(listBlocks, priceScaleBps),
    priceScaleBps,
    mineralShareBps: basisPoints(ledger.terms.mineralShare),
    durationMs: ledger.terms.durationMs,
  };
}

function dinoIsBusy(ledger: RentalLedger, dinoId: string): boolean {
  return ledger.rentals.some((rental) => rental.dinoId === dinoId && rental.status === "active");
}

function putElder(ledger: RentalLedger, shop: ElderShop): RentalLedger {
  const exists = ledger.elders.some((row) => row.elderId === shop.elderId);
  const elders = exists
    ? ledger.elders.map((row) => (row.elderId === shop.elderId ? shop : row))
    : [...ledger.elders, shop];
  return { ...ledger, elders };
}

function putRental(ledger: RentalLedger, rental: RentalRecord, isNew: boolean): RentalLedger {
  const rentals = isNew
    ? [...ledger.rentals, rental]
    : ledger.rentals.map((row) => (row.id === rental.id ? rental : row));
  return { ...ledger, rentals };
}

function blankShop(elderId: string, familyId: string): ElderShop {
  return { elderId, familyId, priceScaleBps: SHARE_SCALE, repByPlayer: {} };
}

export function openRental(ledger: RentalLedger, input: OpenRentalInput): RentalResult<OpenedRental> {
  const id = cleanId(input.id);
  const playerId = cleanId(input.playerId);
  const dinoId = cleanId(input.dinoId);
  const elderId = cleanId(input.elderId);
  const familyId = cleanId(input.familyId);
  if (!id || !playerId || !dinoId || !elderId || !familyId) {
    return no("bad-id", "Rental, player, dino, elder, and family ids are required.");
  }
  const startedMs = parseTime(input.at);
  if (startedMs === null) return no("bad-time", "Open time is not a time.");
  if (ledger.rentals.some((rental) => rental.id === id)) {
    return no("duplicate-id", `Rental ${id} is already on the ledger.`);
  }
  if (dinoIsBusy(ledger, dinoId)) {
    return no("dino-busy", `${dinoId} already has an active rental.`);
  }
  const existing = elderOf(ledger, elderId);
  if (existing && existing.familyId !== familyId) {
    return no("elder-family", `${elderId} already keeps shop for ${existing.familyId}.`);
  }
  if (!Number.isInteger(input.blocks) || input.blocks < 0) {
    return no("bad-blocks", "Blocks on hand must be a whole number.");
  }
  const terms = { ...ledger.terms };
  const shop = existing ?? blankShop(elderId, familyId);
  const quote = quoteRental(ledger, elderId);
  if (input.blocks < quote.upfrontBlocks) {
    return no("short-blocks", `Need ${quote.upfrontBlocks} blocks, have ${input.blocks}.`);
  }
  const dueMs = startedMs + terms.durationMs;
  if (!Number.isFinite(dueMs)) return no("bad-time", "The due time fell off the calendar.");
  const rental: RentalRecord = {
    id,
    playerId,
    dinoId,
    elderId,
    familyId,
    startedAt: stamp(startedMs),
    dueAt: stamp(dueMs),
    blocksCharged: quote.upfrontBlocks,
    terms,
    minerals: [],
    status: "active",
    closedAt: null,
    ranOff: false,
  };
  const next = putRental(putElder(ledger, shop), rental, true);
  return yes(next, {
    rental,
    blocksCharged: quote.upfrontBlocks,
    blocksLeft: input.blocks - quote.upfrontBlocks,
    quote,
  });
}

function requireActive(ledger: RentalLedger, rentalId: string): RentalRecord | RentalNo {
  const id = cleanId(rentalId);
  if (!id) return no("bad-id", "Rental id is required.");
  const rental = ledger.rentals.find((row) => row.id === id);
  if (!rental) return no("unknown-rental", `No rental ${id}.`);
  if (rental.status !== "active") return no("not-active", `Rental ${id} is already ${rental.status}.`);
  return rental;
}

export function harvestMineral(
  ledger: RentalLedger,
  rentalId: string,
  mineralId: string,
  count: number,
): RentalResult<RentalRecord> {
  const rental = requireActive(ledger, rentalId);
  if ("ok" in rental) return rental;
  const id = cleanId(mineralId);
  if (!id) return no("bad-mineral", "Mineral id is required.");
  if (!Number.isInteger(count) || count < 1 || count > HARVEST_CAP) {
    return no("bad-mineral", "Harvest count must be a whole number from 1 to 1000000.");
  }
  const minerals = rental.minerals.map((stack) => ({ ...stack }));
  const found = minerals.find((stack) => stack.id === id);
  const nextCount = (found?.count ?? 0) + count;
  if (nextCount > MINERAL_CAP) return no("bad-mineral", `${id} would pass the haul cap.`);
  if (found) found.count = nextCount;
  else minerals.push({ id, count });
  const updated: RentalRecord = { ...rental, minerals };
  return yes(putRental(ledger, updated, false), updated);
}

function readClose(input: string | CloseRentalInput): CloseRentalInput {
  return typeof input === "string" ? { at: input } : input;
}

function addRep(shop: ElderShop, playerId: string, delta: number): { shop: ElderShop; rep: number } {
  const current = shop.repByPlayer[playerId] ?? 0;
  const rep = clampRep(current + delta);
  if (delta === 0 && shop.repByPlayer[playerId] === undefined) return { shop, rep: current };
  return {
    rep,
    shop: { ...shop, repByPlayer: { ...shop.repByPlayer, [playerId]: rep } },
  };
}

function label(value: string | undefined, fallback: string): string {
  const cleaned = (value ?? "").replace(/[\u0000-\u001f]/g, "").trim().slice(0, 120);
  return cleaned.length > 0 ? cleaned : fallback;
}

const DOWNED_FALLBACK = "Quit. I ran off. I am not dead. The elder's prices go up.";

function downedPost(rental: RentalRecord, input: CloseRentalInput): string {
  const dino = label(input.dinoName, rental.dinoId);
  const player = label(input.playerName, rental.playerId);
  const elder = label(input.elderName, rental.elderId);
  const full = `${dino} quit. ${player} downed the rental, so I ran off. ${elder}'s prices go up. I am not dead.`;
  return estimateTokens(full) <= POST_TOKEN_CAP ? full : DOWNED_FALLBACK;
}

function settle(
  ledger: RentalLedger,
  rental: RentalRecord,
  input: CloseRentalInput,
  outcome: Exclude<RentalStatus, "active">,
): RentalResult<RentalSettlement> {
  const closedMs = parseTime(input.at);
  if (closedMs === null) return no("bad-time", "Close time is not a time.");
  const startedMs = Date.parse(rental.startedAt);
  if (closedMs < startedMs) return no("time-backwards", "The rental cannot close before it opened.");
  const shop = elderOf(ledger, rental.elderId) ?? blankShop(rental.elderId, rental.familyId);
  const shareDue = owedMinerals(rental.minerals, rental.terms.mineralShare);
  const repDelta = outcome === "returned"
    ? rental.terms.mineralReturnRep
    : outcome === "plain"
      ? rental.terms.plainReturnRep
      : outcome === "late"
        ? rental.terms.lateRep
        : rental.terms.downedRep;
  const bumped = outcome === "downed";
  const bumpBps = bumped ? basisPoints(rental.terms.downedPriceBump) : 0;
  const room = Math.max(0, MAX_PRICE_SCALE_BPS - shop.priceScaleBps);
  const priceBumpBps = Math.min(bumpBps, room);
  const priceScaleBps = shop.priceScaleBps + priceBumpBps;
  const rated = addRep(shop, rental.playerId, repDelta);
  const nextShop: ElderShop = { ...rated.shop, priceScaleBps };
  const closed: RentalRecord = {
    ...rental,
    status: outcome,
    closedAt: stamp(closedMs),
    ranOff: bumped,
    minerals: rental.minerals.map((stack) => ({ ...stack })),
  };
  const post = bumped ? downedPost(rental, input) : null;
  const settlement: RentalSettlement = {
    rental: closed,
    outcome,
    statKind: outcome === "late" ? "rental_late" : outcome === "downed" ? "rental_downed" : "rental_returned",
    statAmount: 1,
    repDelta,
    rep: rated.rep,
    shareDue,
    priceBumpBps,
    priceScaleBps,
    quit: bumped,
    ranOff: bumped,
    died: false,
    post,
  };
  return yes(putRental(putElder(ledger, nextShop), closed, false), settlement);
}

export function returnRental(
  ledger: RentalLedger,
  rentalId: string,
  input: string | CloseRentalInput,
): RentalResult<RentalSettlement> {
  const rental = requireActive(ledger, rentalId);
  if ("ok" in rental) return rental;
  const close = readClose(input);
  const closedMs = parseTime(close.at);
  if (closedMs === null) return no("bad-time", "Close time is not a time.");
  const dueMs = Date.parse(rental.dueAt);
  if (closedMs > dueMs) return settle(ledger, rental, close, "late");
  const gathered = rental.minerals.some((stack) => stack.count > 0);
  return settle(ledger, rental, close, gathered ? "returned" : "plain");
}

export function downRental(
  ledger: RentalLedger,
  rentalId: string,
  input: string | CloseRentalInput,
): RentalResult<RentalSettlement> {
  const rental = requireActive(ledger, rentalId);
  if ("ok" in rental) return rental;
  return settle(ledger, rental, readClose(input), "downed");
}
