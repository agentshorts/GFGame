/**
 * Family lineage, heir pick, and elder succession.
 * Pure: no clock, no storage, no posts.
 * `simulate.ts` can call these later. This file does not import the simulator.
 *
 * Defaults, all tunable through `FamilyRules`:
 * five living members gank, fewer barter or post a bounty;
 * one spawn per mated family every 30 in-game minutes, cap 8 living;
 * the oldest living member inherits when an elder is killed;
 * dead skin is given on the 5th ask, and 1 hit starts payback.
 * A player who kills an elder is hunted by Big Grumble, who will not be tamed by them
 * until one of those two is dead.
 */

import { estimateTokens } from "../logic/context";
import { clampToTokens } from "./text";
import { BIG_GRUMBLE_ID } from "./tree";
import {
  DEFAULT_FAMILY_RULES,
  POST_TOKEN_CAP,
  type FamilyBook,
  type FamilyPayback,
  type FamilyRules,
  type GrumbleHuntOrder,
  type KillResponse,
  type KinSex,
  type LineageFamily,
  type LineageMember,
  type SkinPester,
  type SuccessionEvent,
} from "./types";

/** Hatchling names. The seed picks one. These are not taken from the roster. */
export const HATCHLING_NAMES = [
  "Bramble",
  "Burr",
  "Cinder",
  "Crumb",
  "Dot",
  "Flint",
  "Kite",
  "Moth",
  "Moss",
  "Nib",
  "Nugget",
  "Puddle",
  "Quill",
  "Soot",
  "Thistle",
  "Wick",
] as const;

export type SuccessionRefusal = "unknown-dino" | "wrong-family" | "not-elder" | "already-dead";

export interface SuccessionOk {
  ok: true;
  book: FamilyBook;
  event: SuccessionEvent;
}

export interface SuccessionNo {
  ok: false;
  reason: SuccessionRefusal;
  book: FamilyBook;
}

export type SuccessionResult = SuccessionOk | SuccessionNo;

export interface SuccessionInput {
  familyId: string;
  slainId: string;
  atMinute: number;
  /** Set when a player landed the kill. Anyone else leaves Big Grumble out of it. */
  killedByPlayerKey?: string | null;
}

export interface Lineage {
  dinoId: string;
  name: string;
  parentIds: string[];
  childIds: string[];
  mateId: string | null;
  /** Founders first. The dino itself is not in this list. */
  ancestorIds: string[];
  /** Children before grandchildren. The dino itself is not in this list. */
  descendantIds: string[];
  generation: number;
}

export type SpawnHoldReason = "early" | "no-pair" | "cap";

export interface SpawnHold {
  familyId: string;
  reason: SpawnHoldReason;
}

export interface Birth {
  familyId: string;
  childId: string;
  parentIds: [string, string];
  atMinute: number;
}

export interface SpawnTick {
  book: FamilyBook;
  seed: number;
  births: Birth[];
  held: SpawnHold[];
}

export type SkinVerdict =
  | { kind: "wait"; asks: number; hits: number; asksLeft: number }
  | { kind: "give-skin"; asks: number; hits: number }
  | { kind: "revenge"; asks: number; hits: number; response: KillResponse | null };

export interface SkinAsk {
  pester: SkinPester;
  verdict: SkinVerdict;
}

export interface SkinHit {
  pester: SkinPester;
  seed: number;
  verdict: SkinVerdict;
}

interface SeedStep {
  /** Half-open unit interval, [0, 1). */
  unit: number;
  seed: number;
}

function whole(value: number, fallback: number, min: number, max: number): number {
  if (!Number.isFinite(value)) return fallback;
  return Math.max(min, Math.min(max, Math.round(value)));
}

function minuteOf(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(1_000_000_000, Math.floor(value)));
}

function ageOf(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(100_000, Math.round(value)));
}

function tally(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.floor(value));
}

/** Unsigned mulberry32 state. Non-finite seeds become 1. */
export function normalizeSeed(seed: number): number {
  if (!Number.isFinite(seed)) return 1;
  return seed >>> 0;
}

/**
 * One mulberry32 step. Same seed, same unit.
 * Spawn sex uses the first step. The hatchling name uses the next.
 * A small family's barter-or-bounty coin uses one step: under 0.5 barters.
 */
export function stepSeed(seed: number): SeedStep {
  let state = normalizeSeed(seed) | 0;
  state = (state + 0x6D2B79F5) | 0;
  let mixed = Math.imul(state ^ (state >>> 15), 1 | state);
  mixed = (mixed + Math.imul(mixed ^ (mixed >>> 7), 61 | mixed)) ^ mixed;
  const unit = ((mixed ^ (mixed >>> 14)) >>> 0) / 4294967296;
  return { unit, seed: state >>> 0 };
}

export function familyRules(partial?: Partial<FamilyRules>): FamilyRules {
  const merged = { ...DEFAULT_FAMILY_RULES, ...partial };
  return {
    gankAt: whole(merged.gankAt, DEFAULT_FAMILY_RULES.gankAt, 1, 100),
    spawnEveryMinutes: whole(merged.spawnEveryMinutes, DEFAULT_FAMILY_RULES.spawnEveryMinutes, 1, 100_000),
    familyCap: whole(merged.familyCap, DEFAULT_FAMILY_RULES.familyCap, 1, 100),
    skinAsks: whole(merged.skinAsks, DEFAULT_FAMILY_RULES.skinAsks, 1, 100),
    revengeHits: whole(merged.revengeHits, DEFAULT_FAMILY_RULES.revengeHits, 1, 100),
  };
}

function blankBook(): FamilyBook {
  return { families: [], dinos: [], lastSpawnMinute: {} };
}

function copyMember(dino: LineageMember): LineageMember {
  return {
    id: dino.id.trim(),
    name: dino.name.trim(),
    familyId: dino.familyId.trim(),
    sex: dino.sex === "male" ? "male" : "female",
    age: ageOf(dino.age),
    alive: dino.alive,
    mateId: dino.mateId?.trim() || null,
    parentIds: dino.parentIds.map((id) => id.trim()).filter((id) => id.length > 0),
    bornAtMinute: minuteOf(dino.bornAtMinute),
  };
}

/** Sort, drop blanks, and keep mate links only when both sides are alive and agree. */
export function finishBook(book: FamilyBook): FamilyBook {
  const families: LineageFamily[] = [];
  const seenFamilies = new Set<string>();
  for (const family of book.families) {
    const familyId = family.familyId.trim();
    if (!familyId || seenFamilies.has(familyId)) continue;
    seenFamilies.add(familyId);
    const name = family.name.trim() || familyId;
    families.push({ familyId, name, elderId: family.elderId?.trim() || null });
  }
  families.sort((a, b) => a.familyId.localeCompare(b.familyId));

  const dinos: LineageMember[] = [];
  const seenDinos = new Set<string>();
  for (const raw of book.dinos) {
    const dino = copyMember(raw);
    if (!dino.id || seenDinos.has(dino.id)) continue;
    seenDinos.add(dino.id);
    if (!dino.name) dino.name = dino.id;
    dinos.push(dino);
  }

  const byId = new Map(dinos.map((dino) => [dino.id, dino]));
  for (const dino of dinos) {
    const parents = [...new Set(dino.parentIds)].filter((id) => id !== dino.id && byId.has(id));
    parents.sort((a, b) => a.localeCompare(b));
    dino.parentIds = parents.slice(0, 2);
    dino.mateId = bondOf(dino, byId);
  }
  dinos.sort((a, b) => a.id.localeCompare(b.id));

  for (const family of families) {
    const elder = family.elderId ? byId.get(family.elderId) : undefined;
    if (!elder || elder.familyId !== family.familyId) family.elderId = null;
  }

  const lastSpawnMinute: Record<string, number> = {};
  const stamps = book.lastSpawnMinute ?? {};
  for (const family of families) {
    const raw = stamps[family.familyId];
    if (typeof raw !== "number" || !Number.isFinite(raw) || raw < 0) continue;
    lastSpawnMinute[family.familyId] = minuteOf(raw);
  }

  return { families, dinos, lastSpawnMinute };
}

function bondOf(dino: LineageMember, byId: ReadonlyMap<string, LineageMember>): string | null {
  if (!dino.alive || !dino.mateId) return null;
  const mate = byId.get(dino.mateId);
  if (!mate || !mate.alive) return null;
  if (mate.mateId !== dino.id) return null;
  if (mate.familyId !== dino.familyId) return null;
  if (mate.sex === dino.sex) return null;
  return mate.id;
}

function member(book: FamilyBook, dinoId: string): LineageMember | null {
  return book.dinos.find((dino) => dino.id === dinoId) ?? null;
}

export function livingMembers(book: FamilyBook, familyId: string): LineageMember[] {
  const clean = finishBook(book);
  return clean.dinos.filter((dino) => dino.alive && dino.familyId === familyId);
}

export function familySize(book: FamilyBook, familyId: string): number {
  return livingMembers(book, familyId).length;
}

/** The breeding pair: oldest combined age, then the id that sorts first. Null without a mutual pair. */
export function matedPair(book: FamilyBook, familyId: string): [LineageMember, LineageMember] | null {
  return pairOf(finishBook(book), familyId);
}

function pairOf(book: FamilyBook, familyId: string): [LineageMember, LineageMember] | null {
  const living = book.dinos.filter((dino) => dino.alive && dino.familyId === familyId && dino.mateId);
  const pairs: [LineageMember, LineageMember][] = [];
  const used = new Set<string>();
  for (const dino of living) {
    if (used.has(dino.id) || !dino.mateId) continue;
    const mate = living.find((row) => row.id === dino.mateId);
    if (!mate || mate.mateId !== dino.id) continue;
    used.add(dino.id);
    used.add(mate.id);
    const ordered: [LineageMember, LineageMember] = dino.id.localeCompare(mate.id) <= 0
      ? [dino, mate]
      : [mate, dino];
    pairs.push(ordered);
  }
  pairs.sort((a, b) => (b[0].age + b[1].age) - (a[0].age + a[1].age) || a[0].id.localeCompare(b[0].id));
  return pairs[0] ?? null;
}

function generationOf(book: FamilyBook, id: string, path: ReadonlySet<string> = new Set()): number {
  if (path.has(id)) return 0;
  const dino = member(book, id);
  if (!dino || dino.parentIds.length === 0) return 0;
  const next = new Set(path);
  next.add(id);
  let max = 0;
  for (const parentId of dino.parentIds) {
    if (!member(book, parentId)) continue;
    max = Math.max(max, generationOf(book, parentId, next) + 1);
  }
  return max;
}

function walkKin(book: FamilyBook, originId: string, direction: "up" | "down"): string[] {
  const found: { id: string; generation: number }[] = [];
  const seen = new Set<string>([originId]);
  let frontier = [originId];
  let guard = 0;
  while (frontier.length > 0 && guard < 10_000) {
    guard += 1;
    const next: string[] = [];
    for (const currentId of frontier) {
      const related = direction === "up"
        ? member(book, currentId)?.parentIds ?? []
        : book.dinos.filter((dino) => dino.parentIds.includes(currentId)).map((dino) => dino.id);
      for (const relatedId of related) {
        if (seen.has(relatedId) || !member(book, relatedId)) continue;
        seen.add(relatedId);
        found.push({ id: relatedId, generation: generationOf(book, relatedId) });
        next.push(relatedId);
      }
    }
    frontier = next;
  }
  found.sort((a, b) => a.generation - b.generation || a.id.localeCompare(b.id));
  return found.map((row) => row.id);
}

export function lineageOf(book: FamilyBook, dinoId: string): Lineage | null {
  const clean = finishBook(book);
  const dino = member(clean, dinoId);
  if (!dino) return null;
  const childIds = clean.dinos.filter((row) => row.parentIds.includes(dinoId)).map((row) => row.id);
  childIds.sort((a, b) => a.localeCompare(b));
  return {
    dinoId: dino.id,
    name: dino.name,
    parentIds: [...dino.parentIds],
    childIds,
    mateId: dino.mateId,
    ancestorIds: walkKin(clean, dino.id, "up"),
    descendantIds: walkKin(clean, dino.id, "down"),
    generation: generationOf(clean, dino.id),
  };
}

/**
 * Oldest living member of the family, besides `exceptId`.
 * Highest age wins. The same age goes to the id that sorts first.
 * Null when nobody is left.
 */
export function pickHeir(book: FamilyBook, familyId: string, exceptId?: string): string | null {
  const living = livingMembers(book, familyId).filter((dino) => dino.id !== exceptId);
  if (living.length === 0) return null;
  const sorted = [...living].sort((a, b) => b.age - a.age || a.id.localeCompare(b.id));
  return sorted[0]!.id;
}

function bury(book: FamilyBook, dinoId: string): FamilyBook {
  const dinos = book.dinos.map((dino) => {
    const parentIds = [...dino.parentIds];
    if (dino.id === dinoId) return { ...dino, alive: false, mateId: null, parentIds };
    if (dino.mateId === dinoId) return { ...dino, mateId: null, parentIds };
    return { ...dino, parentIds };
  });
  return finishBook({
    families: book.families.map((family) => ({ ...family })),
    dinos,
    lastSpawnMinute: { ...book.lastSpawnMinute },
  });
}

/**
 * Mark a non-elder dead and clear the living bond.
 * The sitting elder is left untouched: call `successionEvent` so the seat moves.
 * An unknown id, or a dino who is already dead, returns the same book.
 */
export function noteDeath(book: FamilyBook, dinoId: string): FamilyBook {
  const id = dinoId.trim();
  const dino = book.dinos.find((row) => row.id.trim() === id);
  if (!dino || !dino.alive) return book;
  const family = book.families.find((row) => row.familyId.trim() === dino.familyId.trim());
  if (family?.elderId?.trim() === dino.id.trim()) return book;
  return bury(finishBook(book), id);
}

function playerKeyOf(value: string | null | undefined): string | null {
  if (!value) return null;
  const trimmed = value.replace(/\s+/g, " ").trim();
  if (!trimmed) return null;
  return trimmed.slice(0, 64);
}

function successionLine(input: {
  familyName: string;
  slainName: string;
  heirName: string | null;
  playerKey: string | null;
}): string {
  const seat = input.heirName
    ? `${input.familyName} elder ${input.slainName} is gone. ${input.heirName} is oldest and takes the seat.`
    : `${input.familyName} elder ${input.slainName} is gone. The seat is empty.`;
  const hunt = input.playerKey ? ` Big Grumble hunts ${input.playerKey} and will not be tamed.` : "";
  return clampToTokens(`${seat}${hunt}`, POST_TOKEN_CAP);
}

/**
 * Kill the sitting elder, then seat the oldest living member.
 * Call this while the book still names them as elder. A dead elder who still holds the seat is moved.
 * A second call on the same corpse does not pick again.
 * A player kill attaches a Big Grumble hunt. The book passed in is not changed.
 */
export function successionEvent(book: FamilyBook, input: SuccessionInput): SuccessionResult {
  const slainId = input.slainId.trim();
  const familyId = input.familyId.trim();
  const slain = book.dinos.find((dino) => dino.id.trim() === slainId);
  if (!slain) return { ok: false, reason: "unknown-dino", book };
  const family = book.families.find((row) => row.familyId.trim() === familyId);
  if (!family || slain.familyId.trim() !== familyId) return { ok: false, reason: "wrong-family", book };
  if (family.elderId?.trim() !== slain.id.trim()) {
    return { ok: false, reason: slain.alive ? "not-elder" : "already-dead", book };
  }

  const buried = bury(finishBook(book), slain.id.trim());
  const heirId = pickHeir(buried, familyId, slain.id.trim());
  const next = finishBook({
    ...buried,
    families: buried.families.map((row) => (
      row.familyId === familyId ? { ...row, elderId: heirId } : { ...row }
    )),
  });
  const heir = heirId ? member(next, heirId) : null;
  const seated = next.families.find((row) => row.familyId === familyId) ?? null;
  const playerKey = playerKeyOf(input.killedByPlayerKey);
  const hunt: GrumbleHuntOrder | null = playerKey
    ? { grumbleId: BIG_GRUMBLE_ID, playerKey, untameable: true }
    : null;
  const line = successionLine({
    familyName: seated?.name ?? family.name,
    slainName: slain.name.trim() || slain.id,
    heirName: heir?.name ?? null,
    playerKey,
  });
  return {
    ok: true,
    book: next,
    event: {
      kind: "elder-succession",
      atMinute: minuteOf(input.atMinute),
      familyId,
      familyName: seated?.name ?? familyId,
      slainId: slain.id,
      slainName: slain.name.trim() || slain.id,
      heirId,
      heirName: heir?.name ?? null,
      line,
      tokens: estimateTokens(line),
      hunt,
    },
  };
}

/** True when this player may tame Big Grumble. A live hunt on them blocks it. */
export function grumbleTameAllowed(
  hunt: GrumbleHuntOrder | null,
  playerKey: string,
  life: { grumbleAlive: boolean; playerAlive: boolean },
): boolean {
  if (!hunt) return true;
  const given = playerKeyOf(playerKey);
  if (!given || given !== hunt.playerKey) return true;
  if (!life.grumbleAlive || !life.playerAlive) return true;
  return false;
}

function hatch(seed: number): { sex: KinSex; name: string; seed: number } {
  const sexRoll = stepSeed(seed);
  const nameRoll = stepSeed(sexRoll.seed);
  const index = Math.min(HATCHLING_NAMES.length - 1, Math.floor(nameRoll.unit * HATCHLING_NAMES.length));
  return {
    sex: sexRoll.unit < 0.5 ? "female" : "male",
    name: HATCHLING_NAMES[index] ?? HATCHLING_NAMES[0],
    seed: nameRoll.seed,
  };
}

function childId(book: FamilyBook, familyId: string, atMinute: number): string {
  const base = `kin-${familyId}-${atMinute}`;
  if (!member(book, base)) return base;
  let suffix = 2;
  while (member(book, `${base}-${suffix}`)) suffix += 1;
  return `${base}-${suffix}`;
}

function layEgg(book: FamilyBook, familyId: string, atMinute: number, seed: number): { book: FamilyBook; birth: Birth; seed: number } | null {
  const pair = pairOf(book, familyId);
  if (!pair) return null;
  const hatched = hatch(seed);
  const id = childId(book, familyId, atMinute);
  const parentIds: [string, string] = [pair[0].id, pair[1].id];
  const child: LineageMember = {
    id,
    name: hatched.name,
    familyId,
    sex: hatched.sex,
    age: 0,
    alive: true,
    mateId: null,
    parentIds: [...parentIds],
    bornAtMinute: atMinute,
  };
  const next = finishBook({
    families: book.families,
    dinos: [...book.dinos, child],
    lastSpawnMinute: { ...book.lastSpawnMinute, [familyId]: atMinute },
  });
  return {
    book: next,
    birth: { familyId, childId: id, parentIds, atMinute },
    seed: hatched.seed,
  };
}

/**
 * Minute the next egg would be due. Null when the family has no mated pair, or it is already at the cap.
 * A value at or below the current minute means the next `tickSpawns` can lay one.
 */
export function nextSpawnMinute(book: FamilyBook, familyId: string, rules?: Partial<FamilyRules>): number | null {
  const tuned = familyRules(rules);
  const clean = finishBook(book);
  if (!pairOf(clean, familyId)) return null;
  if (livingCount(clean, familyId) >= tuned.familyCap) return null;
  const last = clean.lastSpawnMinute[familyId] ?? 0;
  return last + tuned.spawnEveryMinutes;
}

function livingCount(book: FamilyBook, familyId: string): number {
  return book.dinos.filter((dino) => dino.alive && dino.familyId === familyId).length;
}

/**
 * Lay at most one egg per family.
 * Due when a mated pair is alive, the living count is under the cap, and 30 in-game minutes
 * have passed since the last spawn (or since minute 0, if they have never spawned).
 * A jump across several windows still lays one egg, then waits a full interval from that minute.
 * Families are visited in id order. Only a birth advances the seed: sex, then name.
 */
export function tickSpawns(book: FamilyBook, atMinute: number, seed: number, rules?: Partial<FamilyRules>): SpawnTick {
  const tuned = familyRules(rules);
  let current = finishBook(book);
  let nextSeed = normalizeSeed(seed);
  const at = minuteOf(atMinute);
  const births: Birth[] = [];
  const held: SpawnHold[] = [];

  for (const family of current.families) {
    const last = current.lastSpawnMinute[family.familyId] ?? 0;
    if (at - last < tuned.spawnEveryMinutes) {
      held.push({ familyId: family.familyId, reason: "early" });
      continue;
    }
    if (!pairOf(current, family.familyId)) {
      held.push({ familyId: family.familyId, reason: "no-pair" });
      continue;
    }
    if (livingCount(current, family.familyId) >= tuned.familyCap) {
      held.push({ familyId: family.familyId, reason: "cap" });
      continue;
    }
    const laid = layEgg(current, family.familyId, at, nextSeed);
    if (!laid) {
      held.push({ familyId: family.familyId, reason: "no-pair" });
      continue;
    }
    current = laid.book;
    nextSeed = laid.seed;
    births.push(laid.birth);
  }

  return { book: current, seed: nextSeed, births, held };
}

/**
 * Big families gank. Smaller families barter or post a bounty.
 * The coin is one seed step, and a gank does not spend it.
 * Null when nobody in that family is alive. The book passed in is not changed.
 */
export function respondToKill(
  book: FamilyBook,
  familyId: string,
  seed: number,
  rules?: Partial<FamilyRules>,
): { response: KillResponse | null; seed: number } {
  const tuned = familyRules(rules);
  const clean = finishBook(book);
  const members = clean.dinos.filter((dino) => dino.alive && dino.familyId === familyId);
  const nextSeed = normalizeSeed(seed);
  if (members.length === 0) return { response: null, seed: nextSeed };
  const memberIds = members.map((dino) => dino.id).sort((a, b) => a.localeCompare(b));
  if (members.length >= tuned.gankAt) {
    return { response: { kind: "gank", familyId, size: members.length, memberIds }, seed: nextSeed };
  }
  const roll = stepSeed(nextSeed);
  const kind: FamilyPayback = roll.unit < 0.5 ? "barter" : "bounty";
  return { response: { kind, familyId, size: members.length, memberIds }, seed: roll.seed };
}

function skinCounts(pester: SkinPester, rules: FamilyRules): SkinPester {
  return {
    asks: Math.min(rules.skinAsks, tally(pester.asks)),
    hits: Math.min(rules.revengeHits, tally(pester.hits)),
  };
}

/** The next ask. A hit that already started payback does not get talked into a gift. */
export function askForSkin(pester: SkinPester, rules?: Partial<FamilyRules>): SkinAsk {
  const tuned = familyRules(rules);
  const current = skinCounts(pester, tuned);
  if (current.hits >= tuned.revengeHits) {
    return {
      pester: current,
      verdict: { kind: "revenge", asks: current.asks, hits: current.hits, response: null },
    };
  }
  const asks = Math.min(tuned.skinAsks, current.asks + 1);
  const next = { asks, hits: current.hits };
  if (asks >= tuned.skinAsks) {
    return { pester: next, verdict: { kind: "give-skin", asks, hits: current.hits } };
  }
  return {
    pester: next,
    verdict: { kind: "wait", asks, hits: current.hits, asksLeft: tuned.skinAsks - asks },
  };
}

/**
 * The hit that reaches the threshold rolls `respondToKill` once.
 * Later hits stay on revenge and do not re-roll the coin.
 */
export function hitForSkin(
  book: FamilyBook,
  familyId: string,
  pester: SkinPester,
  seed: number,
  rules?: Partial<FamilyRules>,
): SkinHit {
  const tuned = familyRules(rules);
  const current = skinCounts(pester, tuned);
  const nextSeed = normalizeSeed(seed);
  if (current.hits >= tuned.revengeHits) {
    return {
      pester: current,
      seed: nextSeed,
      verdict: { kind: "revenge", asks: current.asks, hits: current.hits, response: null },
    };
  }
  const hits = current.hits + 1;
  const next = { asks: current.asks, hits };
  if (hits < tuned.revengeHits) {
    return {
      pester: next,
      seed: nextSeed,
      verdict: { kind: "wait", asks: current.asks, hits, asksLeft: tuned.skinAsks - current.asks },
    };
  }
  const rolled = respondToKill(book, familyId, nextSeed, tuned);
  return {
    pester: next,
    seed: rolled.seed,
    verdict: { kind: "revenge", asks: current.asks, hits, response: rolled.response },
  };
}

export function emptySkinPester(): SkinPester {
  return { asks: 0, hits: 0 };
}

export function emptyFamilyBook(): FamilyBook {
  return blankBook();
}
