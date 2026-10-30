/**
 * Public memory-core contract.
 * Dinobook sim and the persona manager import these types. Keep the names stable.
 * Token counts are the lab estimate: four trimmed characters to one token.
 */

/**
 * `dino-chat-memory/1` (4bb38f1) had no work methods.
 * `/2` adds `getWork`, `putWork`, `deleteWork`, and `creditWages`, and renames the wage field.
 */
export const CORE_CONTRACT = "dino-chat-memory/2";

export const DEFAULT_CONTEXT_BUDGET = 4096;
export const MIN_CONTEXT_BUDGET = 128;
export const MAX_CONTEXT_BUDGET = 8192;

export const TOTAL_FAMILY_ID = "*";

export const RELATIONSHIP_KINDS = [
  "friend",
  "rival",
  "family",
  "elder",
  "prey",
  "ignore",
  "neutral",
] as const;

export type RelationshipKind = (typeof RELATIONSHIP_KINDS)[number];

export const SENTIMENTS = ["like", "dislike", "neutral"] as const;

export type Sentiment = (typeof SENTIMENTS)[number];

export const STAT_KINDS = [
  "kill",
  "elder_kill",
  "hit_taken",
  "hit_dealt",
  "mango_given",
  "mango_refused",
  "block_given",
  "block_refused",
  "rental_returned",
  "rental_late",
  "rental_downed",
  "bounty_claimed",
  "bounty_placed",
] as const;

export type StatKind = (typeof STAT_KINDS)[number];

export const REP_BANDS = ["friendly", "neutral", "wary", "hostile"] as const;

export type RepBand = (typeof REP_BANDS)[number];

export const CONTENT_ACTIONS = ["NONE", "POST", "REPLY", "LIKE", "IGNORE"] as const;

export type ContentAction = (typeof CONTENT_ACTIONS)[number];

/** Chat modes the memory core knows. Same strings as the lab composer. */
export type LabChatMode = "direct" | "all-chat" | "event";

export interface PromptMessage {
  role: "system" | "user" | "assistant";
  content: string;
}

export interface ContextTurn {
  speaker: string;
  text: string;
  role?: "user" | "dino" | "system";
}

export interface TopicSentiment {
  name: string;
  sentiment: Sentiment;
}

export interface MemoryNote {
  at: string;
  text: string;
  tokens: number;
}

export interface DinoMemory {
  dinoId: string;
  summary: string;
  updatedAt: string;
  turnsSinceSummary: number;
}

export interface RelationshipMemory {
  dinoId: string;
  subjectKey: string;
  kind: RelationshipKind;
  /** -10..10 */
  affinity: number;
  /** Capped at the relationship summary knob, default 150 tokens. */
  summary: string;
  lastSeenAt: string;
  interactions: number;
}

export const EMPLOYMENT_KINDS = ["rented", "tamed"] as const;

export type EmploymentKind = (typeof EMPLOYMENT_KINDS)[number];

/**
 * Hook for a later factory-wage design. This lab does not pay wages or run a factory.
 * Mate rank is not a column. Call `mateRank`. Partner choice is `pickPartner` (#103).
 *
 * `lifetimeEarnings` is the sum of DT_Items `Value` × count over material wage stacks.
 * No coins, and no new currency. The total never goes down: spending materials does not reduce it.
 */
export interface DinoWork {
  dinoId: string;
  /** Factory id. Null when nobody employs this dino. */
  employerId: string | null;
  /** Rented shift, or a tame working a factory. Null when unemployed. */
  employment: EmploymentKind | null;
  /**
   * Σ material `Value` × count. Monotonic.
   * A finder's bonus is just more of these stacks, not a separate rank term.
   */
  lifetimeEarnings: number;
  mineralsFound: number;
  /** When true, the dino may ask for a job while a player is in its territory. */
  seekingJob: boolean;
  /**
   * When this dino was first hired, as an ISO time.
   * An earnings tie goes to the earliest hire. Null means "not hired yet" and loses the tie.
   */
  hiredAt: string | null;
}

export interface SpeechQuota {
  maxPerMinute: number;
  maxPerHour: number;
  restSeconds: number;
  maxToSamePlayerPerHour: number;
  /** 0..1. 1 always allows a roll to pass. */
  speakChance: number;
}

export interface InterestThresholds {
  /** Below this, ignore. Equal, like only. Above, reply. */
  interestMin: number;
  /** A dino answers another dino's line at or above this, unless they are rivals. */
  replyToReplyInterest: number;
  /** Own-initiative posts (Dinobook) need interest at or above this. */
  postInterestMin: number;
}

export interface RepBandEdges {
  /** Rep at or above this is friendly. */
  friendlyAt: number;
  /** Rep at or below this, and above hostileAt, is wary. */
  waryAt: number;
  /** Rep at or below this is hostile. */
  hostileAt: number;
}

export interface MemoryKnobs {
  contextBudget: number;
  allChatBudget: number;
  summarizeEvery: number;
  summarizeAtFraction: number;
  dinoSummaryCap: number;
  relationshipSummaryCap: number;
  elderDigestCap: number;
  replyReserve: number;
  quota: SpeechQuota;
  thresholds: InterestThresholds;
  repBands: RepBandEdges;
  repDeltas: Record<StatKind, number>;
  elderIds: string[];
  familyId: string;
}

export interface BudgetSplit {
  total: number;
  persona: number;
  world: number;
  dinoSummary: number;
  relationship: number;
  elderStats: number;
  replyReserve: number;
  recentTurns: number;
}

/** One row of `player_stats`. `familyId` "*" is the totals row. */
export interface PlayerStatsRow {
  playerKey: string;
  familyId: string;
  kills: number;
  elderKills: number;
  hitsTaken: number;
  hitsDealt: number;
  mangoGiven: number;
  mangoRefused: number;
  blocksGiven: number;
  blocksRefused: number;
  rentalsReturned: number;
  rentalsLate: number;
  rentalsDowned: number;
  bountiesClaimed: number;
  bountiesOnPlayer: number;
  /** -100..100, per family. */
  rep: number;
  updatedAt: string;
}

/** Append-only `stat_events` row. */
export interface StatEvent {
  id: number;
  at: string;
  playerKey: string;
  familyId: string;
  dinoId: string;
  kind: StatKind;
  amount: number;
  note: string;
}

export interface StatEventDraft {
  at: string;
  playerKey: string;
  familyId: string;
  dinoId: string;
  kind: StatKind;
  amount?: number;
  note?: string;
}

/** `decision_log` row. `metadata` is stored as JSON text in SQLite. */
export interface DecisionLogEntry {
  id: string;
  at: string;
  cycleId: string;
  dinoId: string;
  subjectKey: string;
  action: string;
  detail: string;
  metadata: Record<string, unknown>;
  engine: string;
  modelId: string | null;
  promptTokens: number | null;
  completionTokens: number | null;
  durationMs: number | null;
}

/** Names from the work issue. Same objects as the types above. */
export type Relationship = RelationshipMemory;
export type PlayerStats = PlayerStatsRow;
export type DecisionLogRow = DecisionLogEntry;

export interface StoredPersona {
  id: string;
  owner: string;
  version: number;
  updatedAt: string;
  json: string;
}

export interface PersonaSave {
  id: string;
  owner: string;
  /** Version the caller last read. 0 creates a row. A mismatch is a 409. */
  version: number;
  json: string;
  updatedAt?: string;
}

export interface QuotaLogEntry {
  at: string;
  dinoId: string;
  subjectKey: string;
  action: "reply" | "like" | "post" | "ignore" | "skip";
}

export const BUDGET_CAPS = {
  persona: 900,
  world: 150,
  dinoSummary: 250,
  relationship: 150,
  elderStats: 200,
} as const;

/**
 * Reputation band edges. Chat and Dinobook both import this object.
 * Do not copy the numbers into another file.
 *
 * - friendly: rep >= friendlyAt (20)
 * - hostile: rep <= hostileAt (-40)
 * - wary: rep <= waryAt (-8), and above hostile
 * - neutral: everything left in between
 */
export const DEFAULT_REP_BANDS: RepBandEdges = {
  friendlyAt: 20,
  waryAt: -8,
  hostileAt: -40,
};

/**
 * Same object as `DEFAULT_REP_BANDS`, frozen.
 * The name Dinobook should import. Callers cannot rewrite the edges.
 */
export const REP_BAND_THRESHOLDS: Readonly<RepBandEdges> = Object.freeze(DEFAULT_REP_BANDS);

/**
 * Rep change per event. Counts use `amount`; rep uses `delta * amount`, then clamps.
 * Unspecified kinds in the source notes are chosen here and listed in the PR.
 */
export const DEFAULT_REP_DELTAS: Record<StatKind, number> = {
  kill: -8,
  elder_kill: -25,
  hit_taken: 0,
  hit_dealt: -3,
  mango_given: 3,
  mango_refused: -2,
  block_given: 2,
  block_refused: -1,
  rental_returned: 2,
  rental_late: -3,
  rental_downed: -6,
  bounty_claimed: -10,
  bounty_placed: -5,
};

/** Affinity change on the relationship with the player who caused the event. */
export const DEFAULT_AFFINITY_DELTAS: Record<StatKind, number> = {
  kill: -6,
  elder_kill: -8,
  hit_taken: 0,
  hit_dealt: -3,
  mango_given: 2,
  mango_refused: -1,
  block_given: 1,
  block_refused: -1,
  rental_returned: 1,
  rental_late: -2,
  rental_downed: -4,
  bounty_claimed: -5,
  bounty_placed: -3,
};

export const STAT_FIELD: Record<StatKind, keyof PlayerStatsRow> = {
  kill: "kills",
  elder_kill: "elderKills",
  hit_taken: "hitsTaken",
  hit_dealt: "hitsDealt",
  mango_given: "mangoGiven",
  mango_refused: "mangoRefused",
  block_given: "blocksGiven",
  block_refused: "blocksRefused",
  rental_returned: "rentalsReturned",
  rental_late: "rentalsLate",
  rental_downed: "rentalsDowned",
  bounty_claimed: "bountiesClaimed",
  bounty_placed: "bountiesOnPlayer",
};

export function estimateTokens(text: string): number {
  const trimmed = text.trim();
  if (!trimmed) return 0;
  return Math.ceil(trimmed.length / 4);
}

export function clampBudget(value: number): number {
  if (!Number.isFinite(value)) return DEFAULT_CONTEXT_BUDGET;
  return Math.min(MAX_CONTEXT_BUDGET, Math.max(MIN_CONTEXT_BUDGET, Math.floor(value)));
}

export function clampInt(value: unknown, fallback: number, min: number, max: number): number {
  const numeric = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(numeric)) return fallback;
  return Math.min(max, Math.max(min, Math.trunc(numeric)));
}

export function clampFloat(value: unknown, fallback: number, min: number, max: number): number {
  const numeric = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(numeric)) return fallback;
  return Math.min(max, Math.max(min, numeric));
}

export function clampRep(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.min(100, Math.max(-100, Math.trunc(value)));
}

export function clampAffinity(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.min(10, Math.max(-10, Math.trunc(value)));
}

export function playerSubjectKey(playerName: string): string {
  const trimmed = playerName.trim().toLowerCase();
  return trimmed.length > 0 ? trimmed : "you";
}

export function repBand(rep: number, edges: RepBandEdges = DEFAULT_REP_BANDS): RepBand {
  if (rep >= edges.friendlyAt) return "friendly";
  if (rep <= edges.hostileAt) return "hostile";
  if (rep <= edges.waryAt) return "wary";
  return "neutral";
}

function cleanDeltas(value: unknown): Record<StatKind, number> {
  const source = value && typeof value === "object" ? value as Partial<Record<StatKind, number>> : {};
  const next = { ...DEFAULT_REP_DELTAS };
  for (const kind of STAT_KINDS) {
    next[kind] = clampInt(source[kind], DEFAULT_REP_DELTAS[kind], -100, 100);
  }
  return next;
}

export function defaultKnobs(): MemoryKnobs {
  return normalizeKnobs({});
}

export function normalizeKnobs(value: unknown): MemoryKnobs {
  const source = value && typeof value === "object" ? value as Partial<MemoryKnobs> : {};
  const quota: Partial<SpeechQuota> = source.quota ?? {};
  const thresholds: Partial<InterestThresholds> = source.thresholds ?? {};
  const bands: Partial<RepBandEdges> = source.repBands ?? {};
  return {
    contextBudget: clampBudget(source.contextBudget ?? DEFAULT_CONTEXT_BUDGET),
    allChatBudget: clampBudget(source.allChatBudget ?? DEFAULT_CONTEXT_BUDGET),
    summarizeEvery: clampInt(source.summarizeEvery, 6, 1, 100),
    summarizeAtFraction: clampFloat(source.summarizeAtFraction, 0.8, 0.1, 1),
    dinoSummaryCap: clampInt(source.dinoSummaryCap, BUDGET_CAPS.dinoSummary, 1, 2000),
    relationshipSummaryCap: clampInt(source.relationshipSummaryCap, BUDGET_CAPS.relationship, 1, 2000),
    elderDigestCap: clampInt(source.elderDigestCap, BUDGET_CAPS.elderStats, 1, 2000),
    replyReserve: clampInt(source.replyReserve, 80, 1, 512),
    quota: {
      maxPerMinute: clampInt(quota.maxPerMinute, 6, 0, 1000),
      maxPerHour: clampInt(quota.maxPerHour, 60, 0, 10000),
      restSeconds: clampInt(quota.restSeconds, 0, 0, 86_400),
      maxToSamePlayerPerHour: clampInt(quota.maxToSamePlayerPerHour, 30, 0, 10000),
      speakChance: clampFloat(quota.speakChance, 1, 0, 1),
    },
    thresholds: {
      interestMin: clampInt(thresholds.interestMin, 4, 1, 10),
      replyToReplyInterest: clampInt(thresholds.replyToReplyInterest, 8, 1, 10),
      postInterestMin: clampInt(thresholds.postInterestMin, 5, 1, 10),
    },
    repBands: {
      friendlyAt: clampInt(bands.friendlyAt, DEFAULT_REP_BANDS.friendlyAt, -100, 100),
      waryAt: clampInt(bands.waryAt, DEFAULT_REP_BANDS.waryAt, -100, 100),
      hostileAt: clampInt(bands.hostileAt, DEFAULT_REP_BANDS.hostileAt, -100, 100),
    },
    repDeltas: cleanDeltas(source.repDeltas),
    elderIds: Array.isArray(source.elderIds) ? source.elderIds.filter((id): id is string => typeof id === "string" && id.length > 0) : [],
    familyId: typeof source.familyId === "string" && source.familyId.trim().length > 0 ? source.familyId.trim() : "pack",
  };
}
