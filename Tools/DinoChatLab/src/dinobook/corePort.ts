import type { DinobookEventId, FamilyBrief, RepBand, WageCard } from "./types";
import { LAB_TREE, type FamilyTree } from "./tree";
import { finishWage, seedWageCards, wageFromUnknown } from "./wages";

/**
 * Thin stand-in for `src/core` on feature/dino-chat-memory.
 * That contract (dino-chat-memory/1, 4bb38f1) has player stats and dino memory,
 * and no wage or mate-rank fields. Wages stay on this stub until a core row
 * grows `wage` / `mateRank` (or `getWages`). `loadCorePort` still binds the
 * real modules when they are in the tree.
 */

export function repBand(rep: number): RepBand {
  if (rep <= -40) return "hostile";
  if (rep <= -10) return "wary";
  if (rep >= 25) return "friendly";
  return "neutral";
}

export function clampRep(rep: number): number {
  if (!Number.isFinite(rep)) return 0;
  return Math.max(-100, Math.min(100, Math.round(rep)));
}

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
      return payRows(playerKey).map((card) => ({ ...card }));
    },
    setWage(playerKey, card) {
      const list = payRows(playerKey);
      const next = finishWage(card);
      const index = list.findIndex((row) => row.dinoId === next.dinoId);
      if (index < 0) list.push(next);
      else list[index] = next;
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
  stats?: Record<string, unknown>;
  memory?: Record<string, unknown>;
  storage?: Record<string, unknown>;
  types?: Record<string, unknown>;
}

function asRows(value: unknown): CoreStatsRow[] {
  if (!Array.isArray(value)) return [];
  return value.filter((row): row is CoreStatsRow => !!row && typeof row === "object");
}

function overlayWages(
  storage: Record<string, unknown>,
  memoryMod: Record<string, unknown>,
  local: DinobookCorePort,
  playerKey: string,
): WageCard[] {
  const base = local.wages(playerKey);
  const patches = new Map<string, Partial<WageCard>>();
  const apply = (raw: unknown, fallbackId: string) => {
    const parsed = wageFromUnknown(raw, fallbackId);
    if (!parsed?.dinoId) return;
    patches.set(parsed.dinoId, { ...patches.get(parsed.dinoId), ...parsed, dinoId: parsed.dinoId });
  };
  const getWages = storage.getWages ?? storage.listWages ?? memoryMod.getWages;
  if (typeof getWages === "function") {
    const raw = (getWages as (key: string) => unknown)(playerKey);
    if (Array.isArray(raw)) {
      for (const row of raw) apply(row, "");
    }
  }
  const getMemory = memoryMod.getMemory ?? storage.getMemory;
  if (typeof getMemory === "function") {
    for (const card of base) {
      apply((getMemory as (id: string) => unknown)(card.dinoId), card.dinoId);
    }
  }
  if (patches.size === 0) return base;
  return base.map((card) => finishWage({ ...card, ...patches.get(card.dinoId), dinoId: card.dinoId }));
}

/**
 * Adapt whatever `src/core` exported. Unknown shapes fall back to the local port
 * method by method, so a partial landing of the other branch still runs.
 */
export function portFromCore(modules: CoreModuleBag, tree: FamilyTree = LAB_TREE): DinobookCorePort | null {
  const storage = modules.storage ?? {};
  const memoryMod = modules.memory ?? {};
  const getStats = storage.getStats ?? storage.getPlayerStats;
  const getMemory = memoryMod.getMemory ?? storage.getMemory;
  const getWages = storage.getWages ?? storage.listWages ?? memoryMod.getWages;
  const putMemory = memoryMod.putMemory ?? memoryMod.remember ?? storage.putMemory;
  if (typeof getStats !== "function" && typeof getMemory !== "function" && typeof getWages !== "function") return null;

  const local = createLocalCore(tree);
  const primed = new Set<string>();
  return {
    source: "src/core",
    briefs(playerKey) {
      if (typeof getStats !== "function") return local.briefs(playerKey);
      const raw = (getStats as (key: string) => unknown)(playerKey);
      const rows = asRows(raw).map((row) => briefFromCoreStats(row));
      const usable = rows.filter((row) => row.familyId !== "*");
      return usable.length > 0 ? usable : local.briefs(playerKey);
    },
    apply(playerKey, familyId, event) {
      local.apply(playerKey, familyId, event);
    },
    setBrief(playerKey, brief) {
      local.setBrief(playerKey, brief);
    },
    wages(playerKey) {
      if (!primed.has(playerKey)) {
        for (const card of overlayWages(storage, memoryMod, local, playerKey)) {
          local.setWage(playerKey, card);
        }
        primed.add(playerKey);
      }
      return local.wages(playerKey);
    },
    setWage(playerKey, card) {
      local.setWage(playerKey, card);
      const putWages = storage.putWages ?? storage.setWages ?? memoryMod.putWages;
      if (typeof putWages === "function") {
        (putWages as (key: string, cards: WageCard[]) => void)(playerKey, local.wages(playerKey));
      }
    },
    recall(dinoId) {
      if (typeof getMemory !== "function") return local.recall(dinoId);
      const raw = (getMemory as (id: string) => unknown)(dinoId);
      if (typeof raw === "string") return raw;
      if (raw && typeof raw === "object" && "summary" in raw && typeof raw.summary === "string") {
        return raw.summary;
      }
      return local.recall(dinoId);
    },
    remember(dinoId, line) {
      local.remember(dinoId, line);
      if (typeof putMemory === "function") {
        (putMemory as (id: string, summary: string) => void)(dinoId, line);
      }
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
