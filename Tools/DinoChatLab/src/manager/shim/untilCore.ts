/**
 * Stand-in for Tools/DinoChatLab/src/core until feature/dino-chat-memory lands.
 * Issue #75 owns those interfaces. Delete this file once src/core/index.ts exports them.
 * Field names follow the conversation-adaptation plan so the swap stays mechanical.
 */

export const RELATIONSHIP_KINDS = ["friend", "rival", "family", "elder", "prey", "ignore", "neutral"] as const;
export type RelationshipKind = (typeof RELATIONSHIP_KINDS)[number];

export const AUTHORED_KINDS = ["friend", "rival", "family"] as const;
export type AuthoredKind = (typeof AUTHORED_KINDS)[number];

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
  affinity: number;
  summary: string;
  lastSeenAt: string;
  interactions: number;
}

export interface SpeechQuota {
  maxPerMinute: number;
  maxPerHour: number;
  restSeconds: number;
  maxToSamePlayerPerHour: number;
  speakChance: number;
}

export interface DecisionLogRow {
  id: string;
  at: string;
  cycleId: string;
  dinoId: string;
  subjectKey: string;
  action: string;
  detail: string;
  metadata: Record<string, unknown>;
  engine: string | null;
  modelId: string | null;
  promptTokens: number | null;
  completionTokens: number | null;
  durationMs: number | null;
}

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

export interface StatEvent {
  at: string;
  playerKey: string;
  familyId: string;
  dinoId: string;
  kind: StatKind;
  amount: number;
  note: string;
}

export interface PlayerStats {
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
  rep: number;
  updatedAt: string;
}

export interface StoredPersona {
  id: string;
  owner: string;
  version: number;
  updatedAt: string;
  record: unknown;
}

/** Methods named in the plan, plus tailLog for the manager's decision-log pane. */
export interface Storage {
  listPersonas(): Promise<StoredPersona[]>;
  savePersona(input: { id: string; version: number; record: unknown }): Promise<StoredPersona>;
  deletePersona(id: string): Promise<void>;
  getMemory(dinoId: string): Promise<DinoMemory | null>;
  putMemory(memory: DinoMemory): Promise<void>;
  getRelationship(dinoId: string, subjectKey: string): Promise<RelationshipMemory | null>;
  putRelationship(memory: RelationshipMemory): Promise<void>;
  appendLog(row: DecisionLogRow): Promise<void>;
  tailLog(dinoId: string, limit: number): Promise<DecisionLogRow[]>;
  getStats(playerKey: string, familyId: string): Promise<PlayerStats | null>;
  bumpStats(event: StatEvent): Promise<PlayerStats>;
}

export const DEFAULT_QUOTA: SpeechQuota = {
  maxPerMinute: 4,
  maxPerHour: 40,
  restSeconds: 0,
  maxToSamePlayerPerHour: 20,
  speakChance: 1,
};

export const STAT_COLUMNS: Record<StatKind, string> = {
  kill: "kills",
  elder_kill: "elder_kills",
  hit_taken: "hits_taken",
  hit_dealt: "hits_dealt",
  mango_given: "mango_given",
  mango_refused: "mango_refused",
  block_given: "blocks_given",
  block_refused: "blocks_refused",
  rental_returned: "rentals_returned",
  rental_late: "rentals_late",
  rental_downed: "rentals_downed",
  bounty_claimed: "bounties_claimed",
  bounty_placed: "bounties_on_player",
};

const STAT_FIELDS: Record<StatKind, keyof PlayerStats> = {
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

/** Local mirror of the PersonaHub stat bump. Rep stays put until #75 owns the delta table. */
export function applyStat(rows: PlayerStats[], event: StatEvent, now: string): PlayerStats {
  const field = STAT_FIELDS[event.kind];
  const touch = (familyId: string): PlayerStats => {
    let row = rows.find((item) => item.playerKey === event.playerKey && item.familyId === familyId);
    if (!row) {
      row = emptyStats(event.playerKey, familyId, now);
      rows.push(row);
    }
    const current = row[field];
    if (typeof current === "number") Object.assign(row, { [field]: current + event.amount });
    row.updatedAt = now;
    return row;
  };
  const updated = touch(event.familyId);
  if (event.familyId !== "*") touch("*");
  return updated;
}

export function emptyStats(playerKey: string, familyId: string, updatedAt: string): PlayerStats {
  return {
    playerKey,
    familyId,
    kills: 0,
    elderKills: 0,
    hitsTaken: 0,
    hitsDealt: 0,
    mangoGiven: 0,
    mangoRefused: 0,
    blocksGiven: 0,
    blocksRefused: 0,
    rentalsReturned: 0,
    rentalsLate: 0,
    rentalsDowned: 0,
    bountiesClaimed: 0,
    bountiesOnPlayer: 0,
    rep: 0,
    updatedAt,
  };
}
