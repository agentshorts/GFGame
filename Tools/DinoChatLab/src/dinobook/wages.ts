import { voiceTag } from "./plans";
import { isElder, type FamilyTree } from "./tree";
import type { RoleId, RosterDino, WageCard } from "./types";

/**
 * Lab jobs. The memory core (dino-chat-memory/1) has no wage or mate-rank fields,
 * so this file is the stub. Factories are named for materials already in DT_Items.
 * They are not workplaces in the game data.
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

const SEED: readonly WageCard[] = [
  { dinoId: "lanternmaw", factoryId: "scales-yard", factoryName: "Scales yard", wage: 6, mateRank: 12 },
  { dinoId: "tusksaw", factoryId: "fossil-pit", factoryName: "Fossil pit", wage: 4, mateRank: 8 },
  { dinoId: "scythepaw", factoryId: "bone-works", factoryName: "Bone works", wage: 9, mateRank: 18 },
  { dinoId: "ripwing", factoryId: "amber-kiln", factoryName: "Amber kiln", wage: 3, mateRank: 6 },
  { dinoId: "chatty-bird", factoryId: "", factoryName: "", wage: 0, mateRank: 2 },
];

export function clampWage(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(99, Math.round(value)));
}

export function clampMateRank(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(100, Math.round(value)));
}

export function factoryById(id: string): Factory | null {
  return LAB_FACTORIES.find((factory) => factory.id === id) ?? null;
}

export function seedWageCards(): WageCard[] {
  return SEED.map((card) => ({ ...card }));
}

export function finishWage(partial: Partial<WageCard> & { dinoId: string }): WageCard {
  const named = LAB_FACTORIES.find((factory) => factory.name === (partial.factoryName ?? ""));
  const factory = factoryById(partial.factoryId ?? "") ?? named ?? null;
  const mateRank = clampMateRank(partial.mateRank ?? 0);
  if (!factory) {
    return { dinoId: partial.dinoId, factoryId: "", factoryName: "", wage: 0, mateRank };
  }
  return {
    dinoId: partial.dinoId,
    factoryId: factory.id,
    factoryName: factory.name,
    wage: clampWage(partial.wage ?? 0),
    mateRank,
  };
}

/** Wages raise mate potential. A zero wage does not. */
export function payShift(card: WageCard): WageCard {
  const next = finishWage(card);
  if (next.wage <= 0) return next;
  return { ...next, mateRank: clampMateRank(next.mateRank + next.wage) };
}

export function payAll(cards: readonly WageCard[]): WageCard[] {
  return cards.map((card) => payShift(card));
}

export function wantedFactory(cards: readonly WageCard[]): Factory {
  const employed = cards.filter((card) => card.wage > 0 && card.factoryId);
  const best = [...employed].sort((a, b) => b.wage - a.wage || a.dinoId.localeCompare(b.dinoId))[0];
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

/** Highest wage leads. Then a find, a complaint, and anyone still looking. */
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
    if (b.wage !== a.wage) return b.wage - a.wage;
    const aElder = isElder(tree, a.dinoId) ? 0 : 1;
    const bElder = isElder(tree, b.dinoId) ? 0 : 1;
    return aElder - bElder || a.dinoId.localeCompare(b.dinoId);
  })[0]!;
  used.add(bragger.dinoId);
  seats.push({
    card: bragger,
    role: bragger.wage > 0 ? "brag-pay" : "job-seeking",
    interest: 9,
  });

  const mineralPref = ["fossil-pit", "amber-kiln", "scales-yard", "bone-works"];
  const miners = known.filter((card) => card.wage > 0 && !used.has(card.dinoId) && mineralOf(card));
  let miner: WageCard | null = null;
  for (const factoryId of mineralPref) {
    miner = miners.find((card) => card.factoryId === factoryId) ?? null;
    if (miner) break;
  }
  if (!miner) miner = miners[0] ?? null;
  const complainers = known
    .filter((card) => card.wage > 0 && !used.has(card.dinoId) && card.dinoId !== miner?.dinoId)
    .sort((a, b) => a.wage - b.wage || a.dinoId.localeCompare(b.dinoId));
  const complainer = complainers[0];
  if (complainer) {
    used.add(complainer.dinoId);
    seats.push({ card: complainer, role: "complain-work", interest: 7 });
  }
  if (miner) {
    used.add(miner.dinoId);
    seats.push({ card: miner, role: "mineral-find", interest: 8 });
  }

  for (const card of known.filter((row) => row.wage <= 0 && !used.has(row.dinoId))) {
    seats.push({ card, role: "job-seeking", interest: 6 });
  }
  return seats;
}

export function bragLine(dinoId: string, card: WageCard): string {
  return `${voiceTag(dinoId)} ${card.factoryName} paid ${card.wage}. Mate rank is ${card.mateRank}. Payday looks good from here.`;
}

export function mineralLine(dinoId: string, card: WageCard, priorName: string): string {
  const mineral = mineralOf(card) || "a stone";
  return `${voiceTag(dinoId)} ${priorName} talked pay. Found ${mineral} at ${card.factoryName}.`;
}

export function complainLine(dinoId: string, card: WageCard, priorName: string): string {
  return `${voiceTag(dinoId)} ${priorName} said it, so I'll say the work was rough. ${card.factoryName} paid ${card.wage}. Long shift.`;
}

export function jobLine(dinoId: string, priorName: string, factoryName: string): string {
  const heard = priorName ? `${priorName} said it, so ` : "";
  return `${voiceTag(dinoId)} ${heard}I'm looking for a job at ${factoryName}.`;
}

export function paydayDemand(card: WageCard, opening: Factory): string {
  if (card.wage <= 0) return `Looking for a job at ${opening.name}.`;
  return `Paid ${card.wage} at ${card.factoryName}. Mate rank ${card.mateRank}.`;
}

function numOrNull(value: unknown): number | null {
  if (typeof value !== "number" && typeof value !== "string") return null;
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return null;
  return n;
}

/** Pull wage / mate rank off a core row if a later contract grows those fields. */
export function wageFromUnknown(raw: unknown, dinoId: string): Partial<WageCard> | null {
  if (!raw || typeof raw !== "object") return null;
  const row = raw as Record<string, unknown>;
  const id = typeof row.dinoId === "string" ? row.dinoId
    : typeof row.dino_id === "string" ? row.dino_id
      : dinoId;
  const wage = numOrNull(row.wage ?? row.wages);
  const mate = numOrNull(row.mateRank ?? row.mate_rank ?? row.matePotential ?? row.mate_potential);
  const factoryId = typeof row.factoryId === "string" ? row.factoryId
    : typeof row.factory_id === "string" ? row.factory_id
      : undefined;
  const factoryName = typeof row.factoryName === "string" ? row.factoryName
    : typeof row.factory === "string" ? row.factory
      : undefined;
  if (wage == null && mate == null && !factoryId && !factoryName) return null;
  return {
    dinoId: id,
    ...(wage == null ? {} : { wage }),
    ...(mate == null ? {} : { mateRank: mate }),
    ...(factoryId == null ? {} : { factoryId }),
    ...(factoryName == null ? {} : { factoryName }),
  };
}
