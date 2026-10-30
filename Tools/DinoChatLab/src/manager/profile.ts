import { filterOutput } from "../logic/filter";
import { parsePersona } from "../logic/persona";
import type { Persona } from "../logic/types";
import { AUTHORED_KINDS, type AuthoredKind } from "./shim/untilCore";

export const SPEECH_PATTERNS = ["CASUAL", "FORMAL", "EXPRESSIVE"] as const;
export type SpeechPattern = (typeof SPEECH_PATTERNS)[number];

export const SENTIMENTS = ["like", "dislike", "neutral"] as const;
export type Sentiment = (typeof SENTIMENTS)[number];

export interface Topic {
  name: string;
  sentiment: Sentiment;
}

export interface AuthoredRelationship {
  subjectKey: string;
  kind: AuthoredKind;
  affinity: number;
}

export interface InterestMins {
  engagement: number;
  post: number;
  comment: number;
  like: number;
}

export interface DinoProfile {
  speechPattern: SpeechPattern;
  topics: Topic[];
  relationships: AuthoredRelationship[];
  interest: InterestMins;
}

export interface ManagedDino {
  persona: Persona;
  profile: DinoProfile;
}

export const DEFAULT_INTEREST: InterestMins = {
  engagement: 4,
  post: 5,
  comment: 4,
  like: 4,
};

export const PERSONA_JSON_MAX = 32 * 1024;

const COLORS = ["#c4a574", "#6ea8a0", "#d37b6a", "#8e7cc3", "#7ea36b", "#d4b15a"];

const AFFINITY: Record<AuthoredKind, number> = {
  friend: 4,
  family: 3,
  rival: -4,
};

export function affinityFor(kind: AuthoredKind): number {
  return AFFINITY[kind];
}

export function inferPattern(speechStyle: string): SpeechPattern {
  const upper = speechStyle.toUpperCase();
  if (upper.includes("FORMAL")) return "FORMAL";
  if (upper.includes("CASUAL")) return "CASUAL";
  if (upper.includes("EXPRESSIVE")) return "EXPRESSIVE";
  return "EXPRESSIVE";
}

export function profileFromPersona(persona: Persona): DinoProfile {
  return {
    speechPattern: inferPattern(persona.speechStyle),
    topics: [],
    relationships: [],
    interest: { ...DEFAULT_INTEREST },
  };
}

export function slugId(name: string, taken: Set<string>): string {
  const base = name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 24) || "dino";
  let id = base;
  let n = 2;
  while (taken.has(id) || !/^[a-z0-9-]+$/.test(id)) {
    id = `${base}-${n}`;
    n += 1;
  }
  return id;
}

export function blankDino(taken: Set<string>, colorIndex = 0): ManagedDino {
  const name = "New Dino";
  const id = slugId(name, taken);
  const persona: Persona = {
    id,
    name,
    color: COLORS[colorIndex % COLORS.length] ?? COLORS[0]!,
    alignment: "N",
    wants: [{ id: "mango-block", name: "Mango block" }],
    personality: "A lab dino still finding a voice.",
    speechStyle: "CASUAL",
    pgRule: "Keep it PG. No slurs, no sexual content, and no graphic harm.",
    maxReplyLength: 200,
    exampleLinesCanon: false,
    exampleLines: ["Hello."],
    loreFromRepo: [],
    loreTodo: ["TODO: created in the persona manager. No game biography yet."],
    greeting: "Hello.",
    farewell: "Bye.",
    fallbackReplies: ["Huh."],
    cues: [{ keywords: ["hello"], replies: ["Hello."] }],
  };
  return {
    persona: parsePersona(persona),
    profile: {
      speechPattern: "CASUAL",
      topics: [],
      relationships: [],
      interest: { ...DEFAULT_INTEREST },
    },
  };
}

function clampLevel(value: unknown, fallback: number): number {
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(10, Math.max(1, Math.round(n)));
}

function clampAffinity(value: unknown, fallback: number): number {
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(10, Math.max(-10, Math.round(n)));
}

export function sanitizeProfile(raw: unknown, persona: Persona): DinoProfile {
  const body = raw && typeof raw === "object" ? raw as Record<string, unknown> : {};
  const pattern = SPEECH_PATTERNS.includes(body.speechPattern as SpeechPattern)
    ? body.speechPattern as SpeechPattern
    : inferPattern(persona.speechStyle);
  const topics: Topic[] = [];
  const seen = new Set<string>();
  if (Array.isArray(body.topics)) {
    for (const item of body.topics) {
      if (!item || typeof item !== "object") continue;
      const topic = item as Record<string, unknown>;
      const name = typeof topic.name === "string" ? topic.name.trim().slice(0, 32) : "";
      if (name.length < 2) continue;
      const key = name.toLowerCase();
      if (seen.has(key)) continue;
      seen.add(key);
      const sentiment = SENTIMENTS.includes(topic.sentiment as Sentiment) ? topic.sentiment as Sentiment : "neutral";
      topics.push({ name, sentiment });
      if (topics.length >= 24) break;
    }
  }
  const relationships: AuthoredRelationship[] = [];
  const seenSubjects = new Set<string>();
  if (Array.isArray(body.relationships)) {
    for (const item of body.relationships) {
      if (!item || typeof item !== "object") continue;
      const rel = item as Record<string, unknown>;
      const subjectKey = typeof rel.subjectKey === "string" ? rel.subjectKey.trim().slice(0, 40) : "";
      if (subjectKey.length < 1 || /[\u0000-\u001f]/.test(subjectKey)) continue;
      const subjectId = subjectKey.toLowerCase();
      if (seenSubjects.has(subjectId)) continue;
      seenSubjects.add(subjectId);
      const kind = AUTHORED_KINDS.includes(rel.kind as AuthoredKind) ? rel.kind as AuthoredKind : "friend";
      relationships.push({
        subjectKey,
        kind,
        affinity: clampAffinity(rel.affinity, affinityFor(kind)),
      });
      if (relationships.length >= 24) break;
    }
  }
  const interestRaw = body.interest && typeof body.interest === "object"
    ? body.interest as Record<string, unknown>
    : {};
  return {
    speechPattern: pattern,
    topics,
    relationships,
    interest: {
      engagement: clampLevel(interestRaw.engagement, DEFAULT_INTEREST.engagement),
      post: clampLevel(interestRaw.post, DEFAULT_INTEREST.post),
      comment: clampLevel(interestRaw.comment, DEFAULT_INTEREST.comment),
      like: clampLevel(interestRaw.like, DEFAULT_INTEREST.like),
    },
  };
}

export function pgProblem(dino: ManagedDino): string | null {
  const lines = [
    dino.persona.name,
    dino.persona.personality,
    dino.persona.speechStyle,
    dino.persona.greeting,
    dino.persona.farewell,
    ...dino.persona.exampleLines,
    ...dino.persona.fallbackReplies,
    ...dino.persona.cues.flatMap((cue) => cue.replies),
    ...dino.profile.topics.map((topic) => topic.name),
    ...dino.profile.relationships.map((rel) => rel.subjectKey),
  ];
  for (const line of lines) {
    if (filterOutput(line).filtered) return "That text is not PG.";
  }
  return null;
}

export function topicKeyword(name: string): string | null {
  const first = name.toLowerCase().split(/[^a-z0-9]+/).find(Boolean) ?? "";
  return /^[a-z][a-z0-9]*$/.test(first) ? first : null;
}

function plainClone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

export function personaForTest(dino: ManagedDino): Persona {
  const persona = plainClone(dino.persona);
  persona.speechStyle = persona.speechStyle.slice(0, 256);
  const seen = new Set(persona.cues.flatMap((cue) => cue.keywords));
  for (const topic of dino.profile.topics) {
    const keyword = topicKeyword(topic.name);
    if (!keyword || seen.has(keyword)) continue;
    seen.add(keyword);
    const reply = topic.sentiment === "like"
      ? `${topic.name} is a good subject.`
      : topic.sentiment === "dislike"
        ? `Not a fan of ${topic.name}.`
        : `${topic.name} is fine.`;
    persona.cues.push({ keywords: [keyword], replies: [reply] });
  }
  return parsePersona(persona);
}

export function normalizeManaged(id: string, raw: unknown): { ok: true; dino: ManagedDino; json: string } | { ok: false; error: string } {
  if (!raw || typeof raw !== "object") return { ok: false, error: "Persona JSON is invalid." };
  const body = raw as { persona?: unknown; profile?: unknown };
  try {
    const persona = parsePersona(body.persona);
    if (persona.id !== id) return { ok: false, error: "Persona id does not match." };
    if (persona.speechStyle.length > 256) {
      return { ok: false, error: "Speech style has to be 256 characters or fewer." };
    }
    const profile = sanitizeProfile(body.profile, persona);
    const dino = { persona, profile };
    const problem = pgProblem(dino);
    if (problem) return { ok: false, error: problem };
    const json = JSON.stringify(dino);
    if (json.length > PERSONA_JSON_MAX) return { ok: false, error: "Persona is larger than 32 KB." };
    return { ok: true, dino, json };
  } catch (error) {
    const message = error instanceof Error ? error.message : "Persona JSON is invalid.";
    return { ok: false, error: message.slice(0, 300) };
  }
}

export function exportDocument(dino: ManagedDino): string {
  return JSON.stringify({ ...dino.persona, profile: dino.profile }, null, 2);
}

export function importDocument(data: unknown, taken: Set<string>): ManagedDino {
  if (!data || typeof data !== "object") throw new Error("That file is not a persona.");
  const body = data as Record<string, unknown>;
  const profileRaw = body.profile;
  const persona = parsePersona(body);
  if (taken.has(persona.id)) {
    persona.id = slugId(`${persona.id}-copy`, taken);
  }
  if (persona.speechStyle.length > 256) persona.speechStyle = persona.speechStyle.slice(0, 256);
  const dino = { persona: parsePersona(persona), profile: sanitizeProfile(profileRaw, persona) };
  const problem = pgProblem(dino);
  if (problem) throw new Error(problem);
  return dino;
}
