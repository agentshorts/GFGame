import type { CreatureEvent, CreatureMood, CreatureSpecies } from "../data/creature/types";
import type { EngineId, Persona, ReplyStats } from "../logic/types";
import type { ChatMode } from "../logic/types";

export interface EngineRequest {
  persona: Persona;
  history: { speaker: string; role: "user" | "dino" | "system"; text: string }[];
  utterance: string;
  mode: ChatMode;
  systemPrompt: string;
  temperature: number;
  maxTokens: number;
  peerNames: string[];
  /** Per-dino context window. History is trimmed to this, leaving maxTokens for the reply. */
  contextBudget?: number;
  /** Creature talk only. The classic engines ignore it. */
  species?: CreatureSpecies;
  mood?: CreatureMood;
  event?: CreatureEvent;
  playerName?: string;
  rng?: () => number;
}

export interface EngineReply {
  text: string;
  engine: EngineId;
  stats?: ReplyStats;
  /** Set when the engine had nothing of its own and used the persona fallback. */
  fellBack?: boolean;
}

export interface ChatEngine {
  id: EngineId;
  label: string;
  blurb: string;
  needsModel: boolean;
  ready(persona: Persona): Promise<void>;
  reply(request: EngineRequest): Promise<EngineReply>;
  reset(personaId?: string): void;
}

export const ENGINE_CHOICES: { id: EngineId; label: string; blurb: string; needsModel: boolean }[] = [
  {
    id: "creature",
    label: "Creature",
    blurb: "Gremlin talk. Mood picks the species rule set. No download.",
    needsModel: false,
  },
  {
    id: "eliza",
    label: "ELIZA",
    blurb: "Keyword script. Works with no download.",
    needsModel: false,
  },
  {
    id: "aiml",
    label: "AIML",
    blurb: "A.L.I.C.E.-style categories.",
    needsModel: false,
  },
  {
    id: "rivescript",
    label: "RiveScript",
    blurb: "Trigger and reply script.",
    needsModel: false,
  },
  {
    id: "megahal",
    label: "MegaHAL",
    blurb: "Markov chatter trained on this dino's lines.",
    needsModel: false,
  },
];
