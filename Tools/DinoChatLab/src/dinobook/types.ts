import type { TraceEvent } from "../logic/traceTypes";
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
  /** One button press. Repeated events get a new id. */
  runId: string;
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
  traceId?: string;
}

export interface AllChatLine {
  id: string;
  at: string;
  runId: string;
  eventId: DinobookEventId;
  speakerId: string;
  speakerName: string;
  color: string;
  text: string;
  tokens: number;
  reason: string;
  filtered: boolean;
  traceId?: string;
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
  runId?: string;
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
  runId: string;
  traces: TraceEvent[];
}

/**
 * Family knobs. Each one is a decision default and can be overridden per call.
 * Living members at or above `gankAt` gank. Under that, the family barters or posts a bounty.
 * A mated family lays one spawn per `spawnEveryMinutes`, and stops at `familyCap` living members.
 * The elder gives dead skin after `skinAsks` asks. `revengeHits` hits start payback.
 */
export const GANK_FAMILY_SIZE = 5;
export const SPAWN_EVERY_MINUTES = 30;
export const FAMILY_CAP = 8;
export const SKIN_ASKS = 5;
export const REVENGE_HITS = 1;

export interface FamilyRules {
  gankAt: number;
  spawnEveryMinutes: number;
  familyCap: number;
  skinAsks: number;
  revengeHits: number;
}

export const DEFAULT_FAMILY_RULES: FamilyRules = {
  gankAt: GANK_FAMILY_SIZE,
  spawnEveryMinutes: SPAWN_EVERY_MINUTES,
  familyCap: FAMILY_CAP,
  skinAsks: SKIN_ASKS,
  revengeHits: REVENGE_HITS,
};

export const KIN_SEXES = ["female", "male"] as const;

export type KinSex = (typeof KIN_SEXES)[number];

/** One dino in a family book. Age is caller-owned. This lab does not run a birthday clock. */
export interface LineageMember {
  id: string;
  name: string;
  familyId: string;
  sex: KinSex;
  /** Higher is older. The heir is the highest age, then the id that sorts first. */
  age: number;
  alive: boolean;
  /** Living bond only. Cleared when either mate dies. Pairing itself is the caller's job. */
  mateId: string | null;
  /** Up to two parents. Kept after those parents die. */
  parentIds: string[];
  /** In-game minute of birth. */
  bornAtMinute: number;
}

export interface LineageFamily {
  familyId: string;
  name: string;
  /**
   * Sitting elder. May still name a dead dino until `successionEvent` moves the seat.
   * Null when the seat is empty.
   */
  elderId: string | null;
}

/** Lineage state `simulate.ts` can keep and pass back in. */
export interface FamilyBook {
  families: LineageFamily[];
  dinos: LineageMember[];
  /**
   * Last in-game minute this family spawned.
   * A missing key has never spawned, so the first egg is due at `spawnEveryMinutes`.
   */
  lastSpawnMinute: Record<string, number>;
}

export type FamilyPayback = "gank" | "barter" | "bounty";

/** What the family does after a kill or a skin hit. Member ids are the living ones. */
export interface KillResponse {
  kind: FamilyPayback;
  familyId: string;
  size: number;
  memberIds: string[];
}

/** How many times this player has asked the elder for dead skin, and how many hits landed. */
export interface SkinPester {
  asks: number;
  hits: number;
}

/**
 * Big Grumble hunts the player who killed an elder.
 * That player cannot tame him until one of the two is dead.
 */
export interface GrumbleHuntOrder {
  grumbleId: string;
  playerKey: string;
  untameable: true;
}

/** Posted later by `simulate.ts`. The line is already inside the Dinobook token cap. */
export interface SuccessionEvent {
  kind: "elder-succession";
  atMinute: number;
  familyId: string;
  familyName: string;
  slainId: string;
  slainName: string;
  heirId: string | null;
  heirName: string | null;
  line: string;
  tokens: number;
  hunt: GrumbleHuntOrder | null;
}
