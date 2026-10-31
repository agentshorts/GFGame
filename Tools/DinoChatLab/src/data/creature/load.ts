import hornbackJson from "./hornback.json";
import nipperJson from "./nipper.json";
import raptorJson from "./raptor.json";
import rexJson from "./rex.json";
import sailbackJson from "./sailback.json";
import {
  CREATURE_EVENTS,
  CREATURE_MOODS,
  CREATURE_SPECIES,
  type CreatureSpecies,
  type CreatureSpeciesBook,
} from "./types";

const RAW = [nipperJson, raptorJson, sailbackJson, rexJson, hornbackJson];

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stringList(value: unknown, label: string): string[] {
  if (!Array.isArray(value) || value.length === 0 || value.some((item) => typeof item !== "string" || item.trim().length === 0)) {
    throw new Error(`${label} needs at least one string.`);
  }
  return value.map((item) => (item as string).trim());
}

export function parseSpeciesBook(value: unknown): CreatureSpeciesBook {
  if (!isRecord(value)) throw new Error("A species book must be an object.");
  if (value.schema !== "creature-rules/1") throw new Error("Species book schema must be creature-rules/1.");
  const species = value.species;
  if (typeof species !== "string" || !(CREATURE_SPECIES as readonly string[]).includes(species)) {
    throw new Error(`Unknown species ${String(species)}.`);
  }
  if (typeof value.voice !== "string" || value.voice.trim().length === 0) {
    throw new Error(`${species} needs a voice.`);
  }
  if (!isRecord(value.moods)) throw new Error(`${species} needs moods.`);
  if (!isRecord(value.events)) throw new Error(`${species} needs events.`);
  const moods = {} as CreatureSpeciesBook["moods"];
  for (const mood of CREATURE_MOODS) {
    const pack = value.moods[mood];
    if (!isRecord(pack)) throw new Error(`${species} ${mood} is missing.`);
    if (!Array.isArray(pack.rules) || pack.rules.length === 0) {
      throw new Error(`${species} ${mood} needs rules.`);
    }
    const rules = pack.rules.map((rule, index) => {
      if (!isRecord(rule) || typeof rule.keyword !== "string" || typeof rule.rank !== "number") {
        throw new Error(`${species} ${mood} rule ${index} is incomplete.`);
      }
      if (!Array.isArray(rule.decompositions) || rule.decompositions.length === 0) {
        throw new Error(`${species} ${mood} rule ${rule.keyword} needs decompositions.`);
      }
      const decompositions = rule.decompositions.map((row) => {
        if (!isRecord(row) || typeof row.pattern !== "string") {
          throw new Error(`${species} ${mood} ${rule.keyword} has a bad pattern.`);
        }
        return { pattern: row.pattern, reassembly: stringList(row.reassembly, `${species} ${mood} ${rule.keyword}`) };
      });
      return { keyword: rule.keyword.trim().toLowerCase(), rank: rule.rank, decompositions };
    });
    const growl = typeof pack.growl === "string" ? pack.growl.trim() : "";
    if (mood === "Angry" && growl.length === 0) throw new Error(`${species} Angry needs a growl.`);
    if (mood !== "Angry" && growl.length > 0) throw new Error(`${species} ${mood} should keep the growl empty.`);
    moods[mood] = {
      noises: stringList(pack.noises, `${species} ${mood} noises`),
      growl,
      questions: stringList(pack.questions, `${species} ${mood} questions`),
      fallbacks: stringList(pack.fallbacks, `${species} ${mood} fallbacks`),
      rules,
    };
  }
  const events = {} as CreatureSpeciesBook["events"];
  for (const event of CREATURE_EVENTS) {
    events[event] = stringList(value.events[event], `${species} ${event}`);
  }
  return {
    schema: "creature-rules/1",
    species: species as CreatureSpecies,
    voice: value.voice.trim(),
    moods,
    events,
  };
}

let cached: CreatureSpeciesBook[] | null = null;

export function loadCreatureBooks(): readonly CreatureSpeciesBook[] {
  if (!cached) cached = RAW.map((book) => parseSpeciesBook(book));
  return cached;
}

export function speciesBook(species: CreatureSpecies): CreatureSpeciesBook {
  const book = loadCreatureBooks().find((row) => row.species === species);
  if (!book) throw new Error(`No rules for ${species}.`);
  return book;
}
