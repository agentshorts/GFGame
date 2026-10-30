import { mateRank, type MateStanding } from "../core/stats";
import { voiceTag } from "./plans";
import { isElder, type FamilyTree } from "./tree";
import type { RoleId, RosterDino, WageCard } from "./types";

/**
 * Lab jobs. Factories are named for materials already in DT_Items.
 * They are not workplaces in the game data.
 * Lifetime earnings are `getWork().lifetimeEarnings`. Mate rank is `mateRank(pool, id)`.
 * Shift pay is only the amount Payday adds.
 */

export interface Factory {
  id: string;
  name: string;
  mineral: string;
}

export const LAB_FACTORIES: readonly Factory[] = [
  { id: "fossil-pit", name: "Fossil pit", mineral: "Fossil" },
  { id: "amber-kiln", name: "Amber kiln", mineral: "Amber" },
  { id: "scales-yard", name: "Scales yard", mineral: "Scales" },
  { id: "bone-works", name: "Bone works", mineral: "Bone" },
];

/** Stable hire date for the seeded crew. Looking-for-work dinos stay null. */
export const LAB_HIRED_AT = "2026-01-01T00:00:00.000Z";

const SEED: readonly WageCard[] = [
  { dinoId: "lanternmaw", factoryId: "scales-yard", factoryName: "Scales yard", shiftPay: 40, lifetimeEarnings: 40, mateRank: 0, hiredAt: LAB_HIRED_AT, mineralsFound: 0, employment: "tamed" },
  { dinoId: "tusksaw", factoryId: "fossil-pit", factoryName: "Fossil pit", shiftPay: 20, lifetimeEarnings: 20, mateRank: 0, hiredAt: LAB_HIRED_AT, mineralsFound: 0, employment: "tamed" },
  { dinoId: "scythepaw", factoryId: "bone-works", factoryName: "Bone works", shiftPay: 500, lifetimeEarnings: 0, mateRank: 0, hiredAt: LAB_HIRED_AT, mineralsFound: 0, employment: "tamed" },
  { dinoId: "ripwing", factoryId: "amber-kiln", factoryName: "Amber kiln", shiftPay: 10, lifetimeEarnings: 10, mateRank: 0, hiredAt: LAB_HIRED_AT, mineralsFound: 0, employment: "tamed" },
  { dinoId: "chatty-bird", factoryId: "", factoryName: "", shiftPay: 0, lifetimeEarnings: 0, mateRank: 0, hiredAt: null, mineralsFound: 0, employment: null },
];

export const LAB_CREW_IDS: readonly string[] = SEED.map((card) => card.dinoId);

export function clampShiftPay(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(10_000, Math.round(value)));
}

export function clampLifetime(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(1_000_000_000, Math.round(value)));
}

function standing(card: WageCard): MateStanding {
  return {
    dinoId: card.dinoId,
    lifetimeEarnings: card.lifetimeEarnings,
    hiredAt: card.hiredAt,
  };
}

/** Place in this crew. 1 is first pick. Delegates to the core's `mateRank(pool, id)`. */
export function placeInCrew(cards: readonly WageCard[], dinoId: string): number {
  return mateRank(cards.map(standing), dinoId);
}

export function withMateRanks(cards: readonly WageCard[]): WageCard[] {
  const finished = cards.map((card) => finishWage(card));
  return finished.map((card) => ({ ...card, mateRank: mateRank(finished.map(standing), card.dinoId) }));
}

export function factoryById(id: string): Factory | null {
  return LAB_FACTORIES.find((factory) => factory.id === id) ?? null;
}

export function seedWageCards(): WageCard[] {
  return withMateRanks(SEED.map((card) => ({ ...card })));
}

export function finishWage(partial: Partial<WageCard> & { dinoId: string }): WageCard {
  const named = LAB_FACTORIES.find((factory) => factory.name === (partial.factoryName ?? ""));
  const factory = factoryById(partial.factoryId ?? "") ?? named ?? null;
  const lifetimeEarnings = clampLifetime(partial.lifetimeEarnings ?? 0);
  const mineralsFound = clampLifetime(partial.mineralsFound ?? 0);
  if (!factory) {
    return {
      dinoId: partial.dinoId,
      factoryId: "",
      factoryName: "",
      shiftPay: 0,
      lifetimeEarnings,
      mateRank: 0,
      hiredAt: null,
      mineralsFound,
      employment: null,
    };
  }
  const hired = typeof partial.hiredAt === "string" && partial.hiredAt.trim().length > 0
    ? partial.hiredAt
    : LAB_HIRED_AT;
  return {
    dinoId: partial.dinoId,
    factoryId: factory.id,
    factoryName: factory.name,
    shiftPay: clampShiftPay(partial.shiftPay ?? 0),
    lifetimeEarnings,
    mateRank: 0,
    hiredAt: hired,
    mineralsFound,
    employment: partial.employment === "rented" ? "rented" : "tamed",
  };
}

/** Shift pay is added to the lifetime. A zero shift does not move it. Rank is filled by `payAll`. */
export function payShift(card: WageCard): WageCard {
  const next = finishWage(card);
  if (next.shiftPay <= 0) return next;
  return finishWage({ ...next, lifetimeEarnings: next.lifetimeEarnings + next.shiftPay });
}

export function payAll(cards: readonly WageCard[]): WageCard[] {
  return withMateRanks(cards.map((card) => payShift(card)));
}

export function wantedFactory(cards: readonly WageCard[]): Factory {
  const employed = cards.filter((card) => card.shiftPay > 0 && card.factoryId);
  const best = [...employed].sort((a, b) => b.shiftPay - a.shiftPay || a.dinoId.localeCompare(b.dinoId))[0];
  return factoryById(best?.factoryId ?? "") ?? LAB_FACTORIES[3]!;
}

export interface PaydaySeat {
  card: WageCard;
  role: RoleId;
  interest: number;
}

function mineralOf(card: WageCard): string {
  return factoryById(card.factoryId)?.mineral ?? "";
}

/** Highest shift leads. Then a complaint, a find, and anyone still looking. */
export function paydaySeats(
  cards: readonly WageCard[],
  roster: readonly RosterDino[],
  tree: FamilyTree,
): PaydaySeat[] {
  const known = cards.filter((card) => roster.some((dino) => dino.id === card.dinoId));
  if (known.length === 0) return [];
  const used = new Set<string>();
  const seats: PaydaySeat[] = [];

  const bragger = [...known].sort((a, b) => {
    if (b.shiftPay !== a.shiftPay) return b.shiftPay - a.shiftPay;
    const aElder = isElder(tree, a.dinoId) ? 0 : 1;
    const bElder = isElder(tree, b.dinoId) ? 0 : 1;
    return aElder - bElder || a.dinoId.localeCompare(b.dinoId);
  })[0]!;
  used.add(bragger.dinoId);
  seats.push({
    card: bragger,
    role: bragger.shiftPay > 0 ? "brag-pay" : "job-seeking",
    interest: 9,
  });

  const mineralPref = ["fossil-pit", "amber-kiln", "scales-yard", "bone-works"];
  const miners = known.filter((card) => card.shiftPay > 0 && !used.has(card.dinoId) && mineralOf(card));
  let miner: WageCard | null = null;
  for (const factoryId of mineralPref) {
    miner = miners.find((card) => card.factoryId === factoryId) ?? null;
    if (miner) break;
  }
  if (!miner) miner = miners[0] ?? null;
  const complainers = known
    .filter((card) => card.shiftPay > 0 && !used.has(card.dinoId) && card.dinoId !== miner?.dinoId)
    .sort((a, b) => a.shiftPay - b.shiftPay || a.dinoId.localeCompare(b.dinoId));
  const complainer = complainers[0];
  if (complainer) {
    used.add(complainer.dinoId);
    seats.push({ card: complainer, role: "complain-work", interest: 7 });
  }
  if (miner) {
    used.add(miner.dinoId);
    seats.push({ card: miner, role: "mineral-find", interest: 8 });
  }

  for (const card of known.filter((row) => row.shiftPay <= 0 && !used.has(row.dinoId))) {
    seats.push({ card, role: "job-seeking", interest: 6 });
  }
  return seats;
}

export function bragLine(dinoId: string, card: WageCard): string {
  return `${voiceTag(dinoId)} ${card.factoryName} paid ${card.shiftPay}. Lifetime earnings ${card.lifetimeEarnings}. Mate rank is ${card.mateRank}. Payday looks good from here.`;
}

export function mineralLine(dinoId: string, card: WageCard, priorName: string): string {
  const mineral = mineralOf(card) || "a stone";
  return `${voiceTag(dinoId)} ${priorName} talked pay. Found ${mineral} at ${card.factoryName}.`;
}

export function complainLine(dinoId: string, card: WageCard, priorName: string): string {
  return `${voiceTag(dinoId)} ${priorName} said it, so I'll say the work was rough. ${card.factoryName} paid ${card.shiftPay}. Long shift.`;
}

export function jobLine(dinoId: string, priorName: string, factoryName: string): string {
  const heard = priorName ? `${priorName} said it, so ` : "";
  return `${voiceTag(dinoId)} ${heard}I'm looking for a job at ${factoryName}.`;
}

export function paydayDemand(card: WageCard, opening: Factory): string {
  if (card.shiftPay <= 0) return `Looking for a job at ${opening.name}.`;
  return `Paid ${card.shiftPay} at ${card.factoryName}. Lifetime ${card.lifetimeEarnings}. Mate rank ${card.mateRank}.`;
}

function numOrNull(value: unknown): number | null {
  if (typeof value !== "number" && typeof value !== "string") return null;
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return null;
  return n;
}

/**
 * Read a core work row. `lifetimeEarnings` wins over `wagesEarned`.
 * Mate rank is not taken from the row.
 */
export function wageFromUnknown(raw: unknown, dinoId: string): Partial<WageCard> | null {
  if (!raw || typeof raw !== "object") return null;
  const row = raw as Record<string, unknown>;
  const id = typeof row.dinoId === "string" ? row.dinoId
    : typeof row.dino_id === "string" ? row.dino_id
      : dinoId;
  const lifetime = "lifetimeEarnings" in row && row.lifetimeEarnings != null
    ? numOrNull(row.lifetimeEarnings)
    : numOrNull(row.wagesEarned);
  const shift = numOrNull(row.shiftPay ?? row.wage ?? row.wages);
  const minerals = numOrNull(row.mineralsFound ?? row.minerals_found);
  const factoryId = typeof row.employerId === "string" ? row.employerId
    : row.employerId === null ? ""
      : typeof row.factoryId === "string" ? row.factoryId
        : typeof row.factory_id === "string" ? row.factory_id
          : undefined;
  const factoryName = typeof row.factoryName === "string" ? row.factoryName
    : typeof row.factory === "string" ? row.factory
      : undefined;
  const hiredAt = "hiredAt" in row
    ? (typeof row.hiredAt === "string" && row.hiredAt.trim().length > 0 ? row.hiredAt : null)
    : undefined;
  const employment = row.employment === "rented" || row.employment === "tamed" ? row.employment : undefined;
  if (lifetime == null && shift == null && minerals == null && factoryId == null && !factoryName && hiredAt === undefined) {
    return null;
  }
  return {
    dinoId: id,
    ...(lifetime == null ? {} : { lifetimeEarnings: lifetime }),
    ...(shift == null ? {} : { shiftPay: shift }),
    ...(minerals == null ? {} : { mineralsFound: minerals }),
    ...(factoryId == null ? {} : { factoryId }),
    ...(factoryName == null ? {} : { factoryName }),
    ...(hiredAt === undefined ? {} : { hiredAt }),
    ...(employment == null ? {} : { employment }),
  };
}
