import {
  CREATURE_EVENTS,
  CREATURE_MOODS,
  CREATURE_SPECIES,
  type CreatureEvent,
  type CreatureMood,
  type CreatureMoodPack,
  type CreatureRule,
  type CreatureSpecies,
  type CreatureSpeciesBook,
} from "../data/creature/types";
import { loadCreatureBooks, speciesBook } from "../data/creature/load";
import type { ChatEngine, EngineReply, EngineRequest } from "./types";

export type { CreatureEvent, CreatureMood, CreatureSpecies };

const REFLECTIONS: Record<string, string> = {
  i: "you",
  you: "i",
  me: "you",
  my: "your",
  mine: "yours",
  myself: "yourself",
  am: "are",
  im: "youre",
  "i'm": "you're",
  are: "am",
  your: "my",
  yours: "mine",
  yourself: "myself",
  was: "were",
  were: "was",
  "i'll": "you'll",
  "i've": "you've",
  "i'd": "you'd",
};

const SWAPS: Record<string, string> = {
  why: "whyy",
  your: "yur",
  you: "yoo",
  hands: "handz",
  smell: "smel",
  berry: "berree",
  the: "da",
  hello: "ello",
  hurt: "hurrt",
  help: "halp",
  family: "famlee",
  gone: "gon",
  bounty: "bownty",
  posted: "postd",
};

/** Lab personas are not the five talk species. This map is a voice key, not a biography. */
const PERSONA_SPECIES: Record<string, CreatureSpecies> = {
  "big-grumble": "Rex",
  tusksaw: "Hornback",
  ripwing: "Sailback",
  scythepaw: "Raptor",
  lanternmaw: "Nipper",
  "chatty-bird": "Nipper",
};

export function speciesForPersona(id: string): CreatureSpecies {
  const key = id.trim().toLowerCase();
  if (PERSONA_SPECIES[key]) return PERSONA_SPECIES[key];
  if (key.includes("rex") || key.includes("grumble")) return "Rex";
  if (key.includes("raptor") || key.includes("scythe")) return "Raptor";
  if (key.includes("sail") || key.includes("ripwing")) return "Sailback";
  if (key.includes("horn") || key.includes("tusk")) return "Hornback";
  return "Nipper";
}

export function tokenize(text: string): string[] {
  return text
    .toLowerCase()
    .replace(/[^a-z0-9']+/g, " ")
    .trim()
    .split(/\s+/)
    .filter((word) => word.length > 0);
}

export function reflectWords(text: string): string {
  return tokenize(text).map((word) => REFLECTIONS[word] ?? word).join(" ");
}

export function mulberry32(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = Math.imul(state ^ (state >>> 15), state | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function seedFrom(parts: readonly string[]): number {
  let hash = 2166136261;
  for (const part of parts) {
    for (let i = 0; i < part.length; i += 1) {
      hash ^= part.charCodeAt(i);
      hash = Math.imul(hash, 16777619);
    }
  }
  return hash >>> 0;
}

function keywordAt(words: readonly string[], keyword: string): number {
  const key = keyword.trim().toLowerCase();
  if (!key || key === "*") return -1;
  return words.indexOf(key);
}

/**
 * `*` captures zero or more words. Literals must match. Captures are in star order.
 * An empty capture is `""`.
 */
export function matchPattern(pattern: string, utterance: string): string[] | null {
  const pat = pattern.trim().toLowerCase().split(/\s+/).filter((token) => token.length > 0);
  const words = tokenize(utterance);
  const caps: string[] = [];

  function walk(pi: number, wi: number): boolean {
    if (pi === pat.length) return wi === words.length;
    const token = pat[pi]!;
    if (token === "*") {
      for (let take = 0; take <= words.length - wi; take += 1) {
        const saved = caps.length;
        caps.push(words.slice(wi, wi + take).join(" "));
        if (walk(pi + 1, wi + take)) return true;
        caps.length = saved;
      }
      return false;
    }
    if (wi < words.length && words[wi] === token) return walk(pi + 1, wi + 1);
    return false;
  }

  if (pat.length === 0) return words.length === 0 ? [] : null;
  return walk(0, 0) ? caps : null;
}

export function reassemble(template: string, captures: readonly string[]): string {
  return template.replace(/[($](\d+)\)?/g, (token, raw: string) => {
    const index = Number(raw) - 1;
    if (!Number.isInteger(index) || index < 0 || index >= captures.length) return token;
    return captures[index] ?? "";
  }).replace(/\s+/g, " ").trim();
}

export interface RulePick {
  rule: CreatureRule;
  pattern: string;
  captures: string[];
  reflected: string[];
  changed: boolean;
}

/** Highest rank wins. A tie prefers the keyword that shows up first, then file order. */
export function pickRule(rules: readonly CreatureRule[], utterance: string): RulePick | null {
  const words = tokenize(utterance);
  const ranked = rules
    .map((rule, index) => ({ rule, index, at: keywordAt(words, rule.keyword) }))
    .filter((row) => row.at >= 0)
    .sort((a, b) => b.rule.rank - a.rule.rank || a.at - b.at || a.index - b.index);
  for (const row of ranked) {
    for (const decomposition of row.rule.decompositions) {
      const captures = matchPattern(decomposition.pattern, utterance);
      if (!captures) continue;
      const reflected = captures.map((part) => reflectWords(part));
      const changed = reflected.some((part, index) => part !== captures[index]);
      return {
        rule: row.rule,
        pattern: decomposition.pattern,
        captures,
        reflected,
        changed,
      };
    }
  }
  return null;
}

export function moodPack(species: CreatureSpecies, mood: CreatureMood): CreatureMoodPack {
  return speciesBook(species).moods[mood];
}

function choose(lines: readonly string[], rng: () => number): string {
  if (lines.length === 0) return "";
  const index = Math.min(lines.length - 1, Math.floor(rng() * lines.length));
  return lines[index] ?? lines[0] ?? "";
}

export interface DraftReply {
  text: string;
  keyword: string | null;
  rank: number | null;
  fellBack: boolean;
  reflected: boolean;
}

/** The line before the twist. Tests pin ranking, reflection, and fallback here. */
export function draftReply(input: {
  species: CreatureSpecies;
  mood: CreatureMood;
  utterance: string;
  rng?: () => number;
}): DraftReply {
  const pack = moodPack(input.species, input.mood);
  const rng = input.rng ?? (() => 0);
  const picked = pickRule(pack.rules, input.utterance);
  if (!picked) {
    return {
      text: choose(pack.fallbacks, rng) || "...",
      keyword: null,
      rank: null,
      fellBack: true,
      reflected: false,
    };
  }
  const templates = pack.rules
    .find((rule) => rule === picked.rule)
    ?.decompositions.find((row) => row.pattern === picked.pattern)
    ?.reassembly ?? [];
  const template = choose(templates, rng);
  return {
    text: reassemble(template, picked.reflected),
    keyword: picked.rule.keyword,
    rank: picked.rule.rank,
    fellBack: false,
    reflected: picked.changed,
  };
}

export function twistLine(
  text: string,
  pack: Pick<CreatureMoodPack, "noises" | "growl" | "questions">,
  rng: () => number,
): string {
  const words = text.split(/\s+/).filter((word) => word.length > 0);
  if (words.length > 0) {
    const start = Math.min(words.length - 1, Math.floor(rng() * words.length));
    let mangled = false;
    for (let step = 0; step < words.length; step += 1) {
      const index = (start + step) % words.length;
      const raw = words[index] ?? "";
      const key = raw.toLowerCase().replace(/[^a-z']/g, "");
      const swapped = SWAPS[key];
      if (!swapped) continue;
      const punct = raw.slice(key.length);
      words[index] = `${swapped}${punct}`;
      if (rng() < 0.45) words.splice(index + 1, 0, swapped);
      mangled = true;
      break;
    }
    if (!mangled && rng() < 0.45) {
      const index = Math.min(words.length - 1, Math.floor(rng() * words.length));
      const word = words[index];
      if (word) words.splice(index, 0, word);
    }
  }
  const noise = pack.noises.length > 0
    ? pack.noises[Math.min(pack.noises.length - 1, Math.floor(rng() * pack.noises.length))] ?? ""
    : "";
  let out = words.join(" ");
  if (noise && !out.toLowerCase().includes(noise.toLowerCase())) out = `${out} ${noise}`.trim();
  const growl = pack.growl.trim();
  if (growl && !out.toLowerCase().includes(growl.toLowerCase())) out = `${growl} ${out}`.trim();
  if (pack.questions.length > 0 && rng() < 0.55) {
    const question = pack.questions[Math.min(pack.questions.length - 1, Math.floor(rng() * pack.questions.length))] ?? "";
    if (question && !out.toLowerCase().includes(question.toLowerCase())) out = `${out} ${question}`.trim();
  }
  return out.replace(/\s+/g, " ").trim();
}

function fillSlots(template: string, slots: Record<string, string>): string {
  return template.replace(/\{([a-z]+)\}/gi, (token, key: string) => {
    const value = slots[key.toLowerCase()];
    return value == null || value.length === 0 ? token : value;
  }).replace(/\s+/g, " ").trim();
}

export interface CreatureTalkInput {
  species: CreatureSpecies;
  mood: CreatureMood;
  utterance?: string;
  event?: CreatureEvent;
  player?: string;
  rng?: () => number;
}

export interface CreatureTalkResult extends DraftReply {
  species: CreatureSpecies;
  mood: CreatureMood;
  text: string;
  growled: boolean;
  event: CreatureEvent | null;
}

function rngFor(input: CreatureTalkInput): () => number {
  if (input.rng) return input.rng;
  return mulberry32(seedFrom([
    input.species,
    input.mood,
    input.event ?? "",
    input.utterance ?? "",
    input.player ?? "",
  ]));
}

export function speakCreature(input: CreatureTalkInput): CreatureTalkResult {
  const pack = moodPack(input.species, input.mood);
  const rng = rngFor(input);
  if (input.event) {
    const player = input.player?.trim() || "you";
    const noise = pack.noises[0] ?? "";
    const template = choose(speciesBook(input.species).events[input.event], rng);
    const draft = fillSlots(template, {
      player,
      noise,
      species: input.species,
      mood: input.mood,
      voice: speciesBook(input.species).voice,
    });
    const text = twistLine(draft, pack, rng);
    return {
      text,
      species: input.species,
      mood: input.mood,
      keyword: null,
      rank: null,
      fellBack: false,
      reflected: false,
      growled: pack.growl.trim().length > 0 && text.toLowerCase().includes(pack.growl.trim().toLowerCase()),
      event: input.event,
    };
  }
  const draft = draftReply({
    species: input.species,
    mood: input.mood,
    utterance: input.utterance ?? "",
    rng,
  });
  const text = twistLine(draft.text, pack, rng);
  return {
    ...draft,
    text,
    species: input.species,
    mood: input.mood,
    growled: pack.growl.trim().length > 0 && text.toLowerCase().includes(pack.growl.trim().toLowerCase()),
    event: null,
  };
}

export function creatureEventLine(input: {
  species: CreatureSpecies;
  mood: CreatureMood;
  event: CreatureEvent;
  player?: string;
  rng?: () => number;
}): CreatureTalkResult {
  return speakCreature(input);
}

export function isCreatureSpecies(value: string): value is CreatureSpecies {
  return (CREATURE_SPECIES as readonly string[]).includes(value);
}

export function isCreatureMood(value: string): value is CreatureMood {
  return (CREATURE_MOODS as readonly string[]).includes(value);
}

export function isCreatureEvent(value: string): value is CreatureEvent {
  return (CREATURE_EVENTS as readonly string[]).includes(value);
}

export function creatureBooks(): readonly CreatureSpeciesBook[] {
  return loadCreatureBooks();
}

export function createCreatureEngine(rng?: () => number): ChatEngine {
  return {
    id: "creature",
    label: "Creature",
    blurb: "Gremlin talk. Mood picks the species rule set. No download.",
    needsModel: false,
    async ready() {},
    async reply(request: EngineRequest): Promise<EngineReply> {
      const species = request.species ?? speciesForPersona(request.persona.id);
      const mood = request.mood ?? "Friendly";
      const spoken = speakCreature({
        species,
        mood,
        utterance: request.utterance,
        event: request.event,
        player: request.playerName,
        rng: rng ?? request.rng,
      });
      return {
        engine: "creature",
        text: spoken.text || "...",
        fellBack: spoken.fellBack,
      };
    },
    reset() {},
  };
}
