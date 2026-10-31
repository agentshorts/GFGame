/**
 * Creature talk rules, shaped like a later Unreal DataTable.
 * One species file holds every mood. Writers add keywords without code.
 * Schema `creature-rules/1`.
 */

export const CREATURE_SPECIES = ["Nipper", "Raptor", "Sailback", "Rex", "Hornback"] as const;

export type CreatureSpecies = (typeof CREATURE_SPECIES)[number];

export const CREATURE_MOODS = ["Friendly", "Wary", "Angry", "Grieving", "Bountied"] as const;

export type CreatureMood = (typeof CREATURE_MOODS)[number];

export const CREATURE_EVENTS = [
  "hurt_by_player",
  "bounty_posted",
  "family_lost",
  "called_for_help",
  "pestered",
] as const;

export type CreatureEvent = (typeof CREATURE_EVENTS)[number];

export interface CreatureDecomposition {
  /** Words and `*` wildcards. Each star is one capture, numbered from 1. */
  pattern: string;
  /** Templates. `(1)` and `$1` drop in the reflected capture. */
  reassembly: string[];
}

export interface CreatureRule {
  keyword: string;
  rank: number;
  decompositions: CreatureDecomposition[];
}

export interface CreatureMoodPack {
  noises: string[];
  /** Spoken on Angry. Empty on the other moods. */
  growl: string;
  questions: string[];
  fallbacks: string[];
  rules: CreatureRule[];
}

export interface CreatureSpeciesBook {
  schema: "creature-rules/1";
  species: CreatureSpecies;
  /** Short name baked into authored lines. */
  voice: string;
  moods: Record<CreatureMood, CreatureMoodPack>;
  events: Record<CreatureEvent, string[]>;
}
