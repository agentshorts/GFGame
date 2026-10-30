import type { DinoWork, StatKind } from "../core/types";
import { clampRep, REP_BAND_THRESHOLDS, repBand as coreRepBand } from "../core/types";
import type { DinobookEventId, FamilyBrief, RepBand, WageCard } from "./types";
import { LAB_TREE, type FamilyTree } from "./tree";
import { clampShiftPay, finishWage, LAB_CREW_IDS, seedWageCards, wageFromUnknown, withMateRanks } from "./wages";

export { clampRep, REP_BAND_THRESHOLDS };

/** Core edges. The numbers live in `REP_BAND_THRESHOLDS`, not in this file. */
export function repBand(rep: number): RepBand {
  return coreRepBand(rep, REP_BAND_THRESHOLDS);
}

/**
 * Binds Dinobook to `src/core` storage methods. A bound port reads only those
 * methods. Seeds stay out until `seedLabCrew`. A shift is `creditWages(dinoId, shiftPay)`,
 * not a `putWork` of the new total. `putMemory` takes a `DinoMemory` object.
 */

function num(value: unknown): number {
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return 0;
  return Math.max(0, Math.round(n));
}

export function emptyBrief(familyId: string): FamilyBrief {
  return {
    familyId,
    rep: 0,
    band: "neutral",
    kills: 0,
    elderKills: 0,
    hitsDealt: 0,
    hitsTaken: 0,
    mangoGiven: 0,
    mangoRefused: 0,
    bountiesClaimed: 0,
    bountiesOnPlayer: 0,
    rentalsReturned: 0,
    rentalsLate: 0,
    rentalsDowned: 0,
  };
}

export function finishBrief(partial: Partial<FamilyBrief> & { familyId: string }): FamilyBrief {
  const rep = clampRep(partial.rep ?? 0);
  return {
    ...emptyBrief(partial.familyId),
    ...partial,
    familyId: partial.familyId,
    rep,
    band: repBand(rep),
    kills: num(partial.kills),
    elderKills: num(partial.elderKills),
    hitsDealt: num(partial.hitsDealt),
    hitsTaken: num(partial.hitsTaken),
    mangoGiven: num(partial.mangoGiven),
    mangoRefused: num(partial.mangoRefused),
    bountiesClaimed: num(partial.bountiesClaimed),
    bountiesOnPlayer: num(partial.bountiesOnPlayer),
    rentalsReturned: num(partial.rentalsReturned),
    rentalsLate: num(partial.rentalsLate),
    rentalsDowned: num(partial.rentalsDowned),
  };
}

/** Seed so a first summon chases: Swamp is hostile and has a kill. The other elders disagree. */
export function seedBrief(familyId: string): FamilyBrief {
  if (familyId === "swamp") {
    return finishBrief({ familyId, rep: -50, kills: 1, mangoRefused: 1 });
  }
  if (familyId === "sickle") return finishBrief({ familyId, rep: 4 });
  if (familyId === "roost") return finishBrief({ familyId, rep: 36, mangoGiven: 2 });
  return emptyBrief(familyId);
}

const DELTA: Record<DinobookEventId, Partial<FamilyBrief>> = {
  "player-joined": {},
  attacked: { kills: 1, hitsDealt: 1, rep: -8 },
  "refused-mango": { mangoRefused: 1, rep: -2 },
  "rental-downed": { rentalsDowned: 1, rep: -6 },
  "summon-big-grumble": {},
  payday: {},
};

export function applyEventDelta(brief: FamilyBrief, event: DinobookEventId): FamilyBrief {
  const delta = DELTA[event];
  return finishBrief({
    ...brief,
    rep: brief.rep + (delta.rep ?? 0),
    kills: brief.kills + (delta.kills ?? 0),
    hitsDealt: brief.hitsDealt + (delta.hitsDealt ?? 0),
    mangoRefused: brief.mangoRefused + (delta.mangoRefused ?? 0),
    rentalsDowned: brief.rentalsDowned + (delta.rentalsDowned ?? 0),
  });
}

export interface DinobookCorePort {
  source: "local" | "src/core";
  briefs(playerKey: string): FamilyBrief[];
  apply(playerKey: string, familyId: string, event: DinobookEventId): void;
  setBrief(playerKey: string, brief: FamilyBrief): void;
  recall(dinoId: string): string;
  remember(dinoId: string, line: string): void;
  wages(playerKey: string): WageCard[];
  setWage(playerKey: string, card: WageCard): void;
  /** Add this shift to the core total. Does not rewrite the total. */
  creditShift(playerKey: string, dinoId: string, shiftPay: number): void;
  /** One mineral find. Does not change lifetime earnings. */
  noteMineral(playerKey: string, dinoId: string): void;
  /** Write the lab demo into storage. A second call does not add it again. */
  seedLabCrew(playerKey: string): void;
}

/** Payday persistence: one `creditWages` per positive shift. Cards' lifetime fields are ignored. */
export function creditPayday(port: DinobookCorePort, playerKey: string, cards: readonly WageCard[]): void {
  for (const card of cards) {
    if (card.shiftPay > 0) port.creditShift(playerKey, card.dinoId, card.shiftPay);
  }
}

export function createLocalCore(tree: FamilyTree = LAB_TREE): DinobookCorePort {
  const stats = new Map<string, FamilyBrief[]>();
  const memory = new Map<string, string>();
  const payroll = new Map<string, WageCard[]>();

  function payRows(playerKey: string): WageCard[] {
    const key = playerKey.trim() || "local";
    let found = payroll.get(key);
    if (!found) {
      found = seedWageCards();
      payroll.set(key, found);
    }
    return found;
  }

  function rows(playerKey: string): FamilyBrief[] {
    const key = playerKey.trim() || "local";
    let found = stats.get(key);
    if (!found) {
      found = tree.families.map((family) => seedBrief(family.familyId));
      stats.set(key, found);
    }
    return found;
  }

  return {
    source: "local",
    briefs(playerKey) {
      return rows(playerKey).map((brief) => ({ ...brief }));
    },
    apply(playerKey, familyId, event) {
      const list = rows(playerKey);
      const index = list.findIndex((brief) => brief.familyId === familyId);
      if (index < 0) return;
      list[index] = applyEventDelta(list[index]!, event);
    },
    setBrief(playerKey, brief) {
      const list = rows(playerKey);
      const next = finishBrief(brief);
      const index = list.findIndex((row) => row.familyId === next.familyId);
      if (index < 0) list.push(next);
      else list[index] = next;
    },
    recall(dinoId) {
      return memory.get(dinoId) ?? "";
    },
    remember(dinoId, line) {
      memory.set(dinoId, line);
    },
    wages(playerKey) {
      return withMateRanks(payRows(playerKey));
    },
    setWage(playerKey, card) {
      const list = payRows(playerKey);
      const next = finishWage(card);
      const index = list.findIndex((row) => row.dinoId === next.dinoId);
      if (index < 0) list.push(next);
      else list[index] = next;
    },
    creditShift(playerKey, dinoId, shiftPay) {
      const shift = clampShiftPay(shiftPay);
      if (shift <= 0) return;
      const list = payRows(playerKey);
      const index = list.findIndex((row) => row.dinoId === dinoId);
      if (index < 0) return;
      const card = list[index]!;
      list[index] = finishWage({ ...card, lifetimeEarnings: card.lifetimeEarnings + shift });
    },
    noteMineral(playerKey, dinoId) {
      const list = payRows(playerKey);
      const index = list.findIndex((row) => row.dinoId === dinoId);
      if (index < 0) return;
      const card = list[index]!;
      list[index] = finishWage({ ...card, mineralsFound: card.mineralsFound + 1 });
    },
    seedLabCrew(playerKey) {
      const key = playerKey.trim() || "local";
      stats.set(key, tree.families.map((family) => seedBrief(family.familyId)));
      payroll.set(key, seedWageCards());
    },
  };
}

/** A player_stats row as the plan describes it, camelCase or snake_case. */
export interface CoreStatsRow {
  player_key?: string;
  playerKey?: string;
  family_id?: string;
  familyId?: string;
  kills?: number;
  elder_kills?: number;
  elderKills?: number;
  hits_taken?: number;
  hitsTaken?: number;
  hits_dealt?: number;
  hitsDealt?: number;
  mango_given?: number;
  mangoGiven?: number;
  mango_refused?: number;
  mangoRefused?: number;
  bounties_claimed?: number;
  bountiesClaimed?: number;
  bounties_on_player?: number;
  bountiesOnPlayer?: number;
  rentals_returned?: number;
  rentalsReturned?: number;
  rentals_late?: number;
  rentalsLate?: number;
  rentals_downed?: number;
  rentalsDowned?: number;
  rep?: number;
}

function pick(row: CoreStatsRow, camel: keyof CoreStatsRow, snake: keyof CoreStatsRow): number {
  const value = row[camel] ?? row[snake];
  return num(value);
}

export function briefFromCoreStats(row: CoreStatsRow, fallbackFamilyId = "*"): FamilyBrief {
  const familyId = String(row.familyId ?? row.family_id ?? fallbackFamilyId);
  return finishBrief({
    familyId,
    rep: Number(row.rep ?? 0),
    kills: pick(row, "kills", "kills"),
    elderKills: pick(row, "elderKills", "elder_kills"),
    hitsTaken: pick(row, "hitsTaken", "hits_taken"),
    hitsDealt: pick(row, "hitsDealt", "hits_dealt"),
    mangoGiven: pick(row, "mangoGiven", "mango_given"),
    mangoRefused: pick(row, "mangoRefused", "mango_refused"),
    bountiesClaimed: pick(row, "bountiesClaimed", "bounties_claimed"),
    bountiesOnPlayer: pick(row, "bountiesOnPlayer", "bounties_on_player"),
    rentalsReturned: pick(row, "rentalsReturned", "rentals_returned"),
    rentalsLate: pick(row, "rentalsLate", "rentals_late"),
    rentalsDowned: pick(row, "rentalsDowned", "rentals_downed"),
  });
}

export interface CoreModuleBag {
  stats?: unknown;
  memory?: unknown;
  storage?: unknown;
  types?: unknown;
}

function asRows(value: unknown): CoreStatsRow[] {
  if (!Array.isArray(value)) return [];
  return value.filter((row): row is CoreStatsRow => !!row && typeof row === "object");
}

const LEDGER_KIND: Partial<Record<DinobookEventId, StatKind>> = {
  attacked: "kill",
  "refused-mango": "mango_refused",
  "rental-downed": "rental_downed",
};

interface StorageMethods {
  getStats?: (playerKey: string) => unknown;
  bumpStats?: (event: unknown, deltas?: unknown) => unknown;
  getMemory?: (dinoId: string) => unknown;
  putMemory?: (memory: unknown) => void;
  getWork?: (dinoId: string) => unknown;
  putWork?: (work: unknown) => void;
  creditWages?: (dinoId: string, amount: number) => unknown;
}

function bindFn<T>(row: Record<string, unknown>, name: string): T | undefined {
  const fn = row[name];
  if (typeof fn !== "function") return undefined;
  return fn.bind(row) as T;
}

function methodBag(value: unknown): StorageMethods | null {
  if (!value || typeof value !== "object") return null;
  const row = value as Record<string, unknown>;
  const bag: StorageMethods = {};
  const getStats = bindFn<NonNullable<StorageMethods["getStats"]>>(row, "getStats");
  const bumpStats = bindFn<NonNullable<StorageMethods["bumpStats"]>>(row, "bumpStats");
  const getMemory = bindFn<NonNullable<StorageMethods["getMemory"]>>(row, "getMemory");
  const putMemory = bindFn<NonNullable<StorageMethods["putMemory"]>>(row, "putMemory");
  const getWork = bindFn<NonNullable<StorageMethods["getWork"]>>(row, "getWork");
  const putWork = bindFn<NonNullable<StorageMethods["putWork"]>>(row, "putWork");
  const creditWages = bindFn<NonNullable<StorageMethods["creditWages"]>>(row, "creditWages");
  if (getStats) bag.getStats = getStats;
  if (bumpStats) bag.bumpStats = bumpStats;
  if (getMemory) bag.getMemory = getMemory;
  if (putMemory) bag.putMemory = putMemory;
  if (getWork) bag.getWork = getWork;
  if (putWork) bag.putWork = putWork;
  if (creditWages) bag.creditWages = creditWages;
  if (!bag.getStats && !bag.bumpStats && !bag.getMemory && !bag.putMemory && !bag.getWork && !bag.putWork && !bag.creditWages) {
    return null;
  }
  return bag;
}

/** An instance, `createDefaultStorage()`, or `new LocalStorageAdapter(memoryKeyValueStore())`. */
export function storageFromModule(value: unknown): StorageMethods | null {
  if (!value || typeof value !== "object") return null;
  const row = value as Record<string, unknown>;
  if (typeof row.createDefaultStorage === "function") {
    const created = methodBag((row.createDefaultStorage as () => unknown)());
    if (created) return created;
  }
  if (typeof row.LocalStorageAdapter === "function" && typeof row.memoryKeyValueStore === "function") {
    const Adapter = row.LocalStorageAdapter as new (store: unknown) => unknown;
    const created = methodBag(new Adapter((row.memoryKeyValueStore as () => unknown)()));
    if (created) return created;
  }
  return methodBag(value);
}

function bindStorage(modules: CoreModuleBag): StorageMethods | null {
  const primary = storageFromModule(modules.storage);
  const extra = storageFromModule(modules.memory);
  if (!primary && !extra) return null;
  return { ...extra, ...primary };
}

function workRecord(card: WageCard): DinoWork {
  const employed = card.factoryId.length > 0;
  return {
    dinoId: card.dinoId,
    employerId: employed ? card.factoryId : null,
    employment: employed ? (card.employment === "rented" ? "rented" : "tamed") : null,
    lifetimeEarnings: card.lifetimeEarnings,
    hiredAt: card.hiredAt,
    mineralsFound: card.mineralsFound,
    seekingJob: !employed,
  };
}

function memorySummary(raw: unknown): string | null {
  if (typeof raw === "string") return raw;
  if (raw && typeof raw === "object" && "summary" in raw && typeof (raw as { summary: unknown }).summary === "string") {
    return (raw as { summary: string }).summary;
  }
  return null;
}

function cardFromWork(raw: unknown, dinoId: string): WageCard | null {
  const parsed = wageFromUnknown(raw, dinoId);
  if (!parsed) return null;
  const card = finishWage({
    dinoId: parsed.dinoId || dinoId,
    shiftPay: 0,
    lifetimeEarnings: 0,
    mineralsFound: 0,
    hiredAt: null,
    factoryId: "",
    factoryName: "",
    employment: null,
    mateRank: 0,
    ...parsed,
  });
  // Shift pay is not a core column. The panel keeps it beside the row.
  return { ...card, shiftPay: 0 };
}

function coreBriefs(bound: StorageMethods, playerKey: string): FamilyBrief[] {
  if (typeof bound.getStats !== "function") return [];
  return asRows(bound.getStats(playerKey))
    .map((row) => briefFromCoreStats(row))
    .filter((row) => row.familyId !== "*");
}

function bump(bound: StorageMethods, playerKey: string, familyId: string, kind: StatKind, amount: number, delta: number): void {
  if (typeof bound.bumpStats !== "function") return;
  bound.bumpStats({
    at: new Date().toISOString(),
    playerKey,
    familyId,
    dinoId: "",
    kind,
    amount,
    note: "dinobook",
  }, { [kind]: delta });
}

/**
 * Bind a storage instance, or a module that can build one.
 * A bag with no storage methods returns null.
 * The result does not keep a local seed, a local memory, or a pinned stat.
 */
export function portFromCore(modules: CoreModuleBag, _tree: FamilyTree = LAB_TREE): DinobookCorePort | null {
  const bound = bindStorage(modules);
  if (!bound) return null;

  return {
    source: "src/core",
    briefs(playerKey) {
      return coreBriefs(bound, playerKey.trim() || "local");
    },
    apply(playerKey, familyId, event) {
      const kind = LEDGER_KIND[event];
      if (!kind) return;
      const delta = event === "attacked" ? -8 : event === "refused-mango" ? -2 : -6;
      bump(bound, playerKey.trim() || "local", familyId, kind, 1, delta);
    },
    setBrief() {
      // Absolute edits are not a core method. The seed button and event bumps are.
    },
    wages() {
      if (typeof bound.getWork !== "function") return [];
      const cards: WageCard[] = [];
      for (const dinoId of LAB_CREW_IDS) {
        const card = cardFromWork(bound.getWork(dinoId), dinoId);
        if (card) cards.push(card);
      }
      return withMateRanks(cards);
    },
    setWage(_playerKey, card) {
      if (typeof bound.putWork !== "function") return;
      const next = finishWage(card);
      const current = typeof bound.getWork === "function" ? cardFromWork(bound.getWork(next.dinoId), next.dinoId) : null;
      bound.putWork(workRecord({
        ...next,
        lifetimeEarnings: current?.lifetimeEarnings ?? next.lifetimeEarnings,
        mineralsFound: next.mineralsFound,
      }));
    },
    creditShift(_playerKey, dinoId, shiftPay) {
      const shift = clampShiftPay(shiftPay);
      if (shift <= 0 || typeof bound.creditWages !== "function") return;
      bound.creditWages(dinoId, shift);
    },
    noteMineral(_playerKey, dinoId) {
      if (typeof bound.getWork !== "function" || typeof bound.putWork !== "function") return;
      const card = cardFromWork(bound.getWork(dinoId), dinoId);
      if (!card) return;
      bound.putWork(workRecord({ ...card, mineralsFound: card.mineralsFound + 1 }));
    },
    seedLabCrew(playerKey) {
      const key = playerKey.trim() || "local";
      if (typeof bound.putWork === "function" && typeof bound.getWork === "function") {
        for (const card of seedWageCards()) {
          if (bound.getWork(card.dinoId)) continue;
          bound.putWork(workRecord(card));
        }
      }
      const have = new Set(coreBriefs(bound, key).map((row) => row.familyId));
      if (!have.has("swamp")) {
        bump(bound, key, "swamp", "kill", 1, -50);
        bump(bound, key, "swamp", "mango_refused", 1, 0);
      }
      if (!have.has("sickle")) bump(bound, key, "sickle", "hit_taken", 1, 4);
      if (!have.has("roost")) bump(bound, key, "roost", "mango_given", 2, 18);
    },
    recall(dinoId) {
      if (typeof bound.getMemory !== "function") return "";
      return memorySummary(bound.getMemory(dinoId)) ?? "";
    },
    remember(dinoId, line) {
      if (typeof bound.putMemory !== "function") return;
      const previous = typeof bound.getMemory === "function" ? bound.getMemory(dinoId) : null;
      const turns = previous && typeof previous === "object" && "turnsSinceSummary" in previous
        && typeof (previous as { turnsSinceSummary: unknown }).turnsSinceSummary === "number"
        ? (previous as { turnsSinceSummary: number }).turnsSinceSummary + 1
        : 0;
      bound.putMemory({
        dinoId,
        summary: line,
        updatedAt: new Date().toISOString(),
        turnsSinceSummary: turns,
      });
    },
  };
}

interface GlobModule {
  [key: string]: unknown;
}

/**
 * Vite only bundles `src/core/*.ts` when those files exist. An empty glob means
 * the memory branch has not landed in this tree.
 */
export async function loadCorePort(tree: FamilyTree = LAB_TREE): Promise<DinobookCorePort> {
  const local = createLocalCore(tree);
  const loaders = import.meta.glob("../core/*.ts") as Record<string, () => Promise<GlobModule>>;
  const keys = Object.keys(loaders);
  if (keys.length === 0) return local;
  const bag: CoreModuleBag = {};
  for (const key of keys) {
    const loaded = await loaders[key]!();
    if (key.endsWith("/stats.ts")) bag.stats = loaded;
    else if (key.endsWith("/memory.ts")) bag.memory = loaded;
    else if (key.endsWith("/storage.ts")) bag.storage = loaded;
    else if (key.endsWith("/types.ts")) bag.types = loaded;
  }
  return portFromCore(bag, tree) ?? local;
}
