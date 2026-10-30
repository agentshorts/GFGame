import type { Alignment } from "../logic/types";

/** Estimated-token cap for one Dinobook post. Four characters count as one token. */
export const POST_TOKEN_CAP = 80;

export const DINOBOOK_EVENTS = [
  { id: "player-joined", label: "Player joined" },
  { id: "attacked", label: "Attacked" },
  { id: "refused-mango", label: "Refused mango" },
  { id: "rental-downed", label: "Rental downed" },
  { id: "payday", label: "Payday" },
  { id: "summon-big-grumble", label: "Summon Big Grumble" },
] as const;

export type DinobookEventId = (typeof DINOBOOK_EVENTS)[number]["id"];

export type RepBand = "friendly" | "neutral" | "wary" | "hostile";

export type PlanId = "welcome" | "toll" | "cover" | "repossess" | "poll" | "payday";

export type RoleId =
  | "lead"
  | "block-path"
  | "take-hit"
  | "screen"
  | "warn"
  | "greet"
  | "report"
  | "verdict"
  | "brag-pay"
  | "mineral-find"
  | "complain-work"
  | "job-seeking";

export type PostLink = "root" | "child" | "side";

export type PostAction = "NONE" | "POST" | "REPLY";

export type GrumbleApproach =
  | "chase"
  | "collect-bounty"
  | "return-rentals"
  | "demand-mango"
  | "play"
  | "watch";

export interface RosterDino {
  id: string;
  name: string;
  alignment: Alignment;
  color: string;
}

/**
 * One dino's factory shift.
 * `lifetimeEarnings` is the core total on `getWork()`.
 * `mateRank` is `mateRank(pool, id)`: 1 is first pick, and it is not stored.
 * `shiftPay` is the lab amount added to the lifetime on Payday.
 */
export interface WageCard {
  dinoId: string;
  factoryId: string;
  factoryName: string;
  /** This shift. Zero means they are looking for a job. Not a core column. */
  shiftPay: number;
  /** Lifetime total from `getWork().lifetimeEarnings`. Not this shift's pay. */
  lifetimeEarnings: number;
  /** Core employment. Null while they are looking. */
  employment: "rented" | "tamed" | null;
  /** Place in the crew. 1 is first pick. 0 means this card is not in a pool yet. */
  mateRank: number;
  /** When they were hired. Null while they are looking. */
  hiredAt: string | null;
  mineralsFound: number;
}

/** Per-family slice of the player record elders quote on Dinobook. */
export interface FamilyBrief {
  familyId: string;
  rep: number;
  band: RepBand;
  kills: number;
  elderKills: number;
  hitsDealt: number;
  hitsTaken: number;
  mangoGiven: number;
  mangoRefused: number;
  bountiesClaimed: number;
  bountiesOnPlayer: number;
  rentalsReturned: number;
  rentalsLate: number;
  rentalsDowned: number;
}

export interface SpamPolicy {
  /** How many distinct dinos may post on one event. */
  maxSpeakersPerEvent: number;
  /** How many posts one dino may make, before the summoner exception. */
  maxPostsPerSpeaker: number;
  /** Big Grumble may add a verdict after his poll. */
  summonerExtraPost: boolean;
  /** How many recent lines, including older events, stay in the dedupe window. */
  dedupeWindow: number;
}

export interface ChatBudget {
  /** All-chat lines allowed for one event. Zero keeps the demand on Dinobook. */
  maxLines: number;
  /** Estimated tokens shared by those lines. */
  maxTokens: number;
  /** When set, only the leader (or Big Grumble, on a summon) speaks in all-chat. */
  leaderOnly: boolean;
}

export const DEFAULT_POLICY: SpamPolicy = {
  maxSpeakersPerEvent: 4,
  maxPostsPerSpeaker: 1,
  summonerExtraPost: true,
  dedupeWindow: 24,
};

export const DEFAULT_CHAT_BUDGET: ChatBudget = {
  maxLines: 1,
  maxTokens: POST_TOKEN_CAP,
  leaderOnly: true,
};

export interface DinobookPost {
  id: string;
  at: string;
  eventId: DinobookEventId;
  authorId: string;
  authorName: string;
  color: string;
  familyId: string | null;
  role: RoleId;
  link: PostLink;
  parentId: string | null;
  content: string;
  tokens: number;
  action: Exclude<PostAction, "NONE">;
  interest: number;
  reason: string;
  filtered: boolean;
}

export interface AllChatLine {
  id: string;
  at: string;
  eventId: DinobookEventId;
  speakerId: string;
  speakerName: string;
  color: string;
  text: string;
  tokens: number;
  reason: string;
  filtered: boolean;
}

export interface PostDecision {
  dinoId: string;
  name: string;
  action: PostAction;
  interest: number;
  reason: string;
  content: string;
}

export interface Skip {
  dinoId: string;
  name: string;
  reason: "speaker-cap" | "duplicate" | "interest" | "chat-budget" | "no-seat";
}

export interface RunInput {
  event: DinobookEventId;
  playerName: string;
  roster: readonly RosterDino[];
  briefs: readonly FamilyBrief[];
  policy?: Partial<SpamPolicy>;
  chat?: Partial<ChatBudget>;
  priorTexts?: readonly string[];
  memory?: Readonly<Record<string, string>>;
  /** Current wage cards. Payday adds shift pay to lifetime earnings. Omit to use the lab seed. */
  wages?: readonly WageCard[];
  at?: string;
  idStart?: number;
}

export interface RunResult {
  posts: DinobookPost[];
  allChat: AllChatLine[];
  decisions: PostDecision[];
  skipped: Skip[];
  planId: PlanId;
  leaderId: string;
  approach: GrumbleApproach | null;
  approachReason: string;
  nextId: number;
  memory: Record<string, string>;
  /** Unchanged, except payday, which returns the cards after the shift. */
  wages: WageCard[];
}
