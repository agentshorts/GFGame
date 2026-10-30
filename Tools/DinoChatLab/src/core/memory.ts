/**
 * Rolling memory, context budgets, interest gates, and speech quotas.
 * Pure functions. Persistence lives in `storage.ts`.
 */

import {
  BUDGET_CAPS,
  clampAffinity,
  clampBudget,
  estimateTokens,
  type BudgetSplit,
  type ContentAction,
  type ContextTurn,
  type DinoMemory,
  type InterestThresholds,
  type LabChatMode,
  type PromptMessage,
  type QuotaLogEntry,
  type RelationshipKind,
  type RelationshipMemory,
  type RepBand,
  type SpeechQuota,
  type TopicSentiment,
} from "./types";

export interface DinoWindow {
  dinoId: string;
  budget: number;
  worldNote: string;
  turns: ContextTurn[];
  /** Turns waiting for the next summary. Session-only; not a SQLite column. */
  pending: ContextTurn[];
  memory: DinoMemory;
}

export interface InterestDecision {
  interest: number;
  action: "ignore" | "like" | "reply";
  shouldLike: boolean;
  reason: string;
  code: "self" | "ignore_mark" | "reply_to_reply" | "below_min" | "at_min" | "above_min";
}

export interface SpeakPlan {
  action: "reply" | "like" | "skip";
  logAction: "reply" | "like" | "ignore" | "skip_quota" | "skip_reply_to_reply";
  reason: string;
  interest: number;
}

export interface ContentDecision {
  action: ContentAction;
  interest: number;
  reason: string;
  shouldLike: boolean;
}

const KIND_BONUS: Record<RelationshipKind, number> = {
  friend: 2,
  family: 2,
  elder: 2,
  rival: 2,
  prey: 1,
  neutral: 0,
  ignore: -4,
};

const PRIORITY: Record<RelationshipKind, number> = {
  friend: 0,
  family: 1,
  elder: 2,
  rival: 3,
  prey: 4,
  neutral: 5,
  ignore: 6,
};

const PENDING_CAP = 24;

export function blankMemory(dinoId: string, at: string): DinoMemory {
  return { dinoId, summary: "", updatedAt: at, turnsSinceSummary: 0 };
}

export function blankRelationship(
  dinoId: string,
  subjectKey: string,
  at: string,
  kind: RelationshipKind = "neutral",
): RelationshipMemory {
  return {
    dinoId,
    subjectKey,
    kind,
    affinity: 0,
    summary: "",
    lastSeenAt: at,
    interactions: 0,
  };
}

export function createDinoWindow(dinoId: string, budget: number, at: string): DinoWindow {
  return {
    dinoId,
    budget: clampBudget(budget),
    worldNote: "",
    turns: [],
    pending: [],
    memory: blankMemory(dinoId, at),
  };
}

export function budgetSplit(total = 4096, replyReserve = 80, elder = true): BudgetSplit {
  const budget = clampBudget(total);
  const wantedReserve = Math.max(0, Math.floor(Number.isFinite(replyReserve) ? replyReserve : 0));
  const reserve = Math.min(wantedReserve, Math.max(0, budget - 1));
  const room = budget - reserve;
  const wantedSum = BUDGET_CAPS.persona
    + BUDGET_CAPS.world
    + BUDGET_CAPS.dinoSummary
    + BUDGET_CAPS.relationship
    + BUDGET_CAPS.elderStats;
  const scale = room >= wantedSum ? 1 : room / wantedSum;
  const persona = Math.floor(BUDGET_CAPS.persona * scale);
  const world = Math.floor(BUDGET_CAPS.world * scale);
  const dinoSummary = Math.floor(BUDGET_CAPS.dinoSummary * scale);
  const relationship = Math.floor(BUDGET_CAPS.relationship * scale);
  const elderStats = elder ? Math.floor(BUDGET_CAPS.elderStats * scale) : 0;
  const used = persona + world + dinoSummary + relationship + elderStats + reserve;
  return {
    total: budget,
    persona,
    world,
    dinoSummary,
    relationship,
    elderStats,
    replyReserve: reserve,
    recentTurns: budget - used,
  };
}

export function messageTokenEstimate(messages: readonly PromptMessage[]): number {
  return messages.reduce((sum, message) => sum + estimateTokens(message.content), 0);
}

export function clampSummary(text: string, tokenCap: number): string {
  const cap = Math.max(0, Math.floor(tokenCap));
  let next = text.trim();
  if (cap === 0) return "";
  while (next.length > 0 && estimateTokens(next) > cap) {
    const room = Math.max(0, cap * 4);
    next = next.slice(0, room).trimEnd();
    const lastSpace = next.lastIndexOf(" ");
    if (lastSpace > room * 0.6) next = next.slice(0, lastSpace).trimEnd();
  }
  return next;
}

export function shouldSummarize(memory: DinoMemory, every = 6): boolean {
  const step = Number.isFinite(every) ? Math.max(1, Math.floor(every)) : 6;
  return memory.turnsSinceSummary >= step;
}

export function overBudgetFraction(tokens: number, budget: number, fraction = 0.8): boolean {
  if (!Number.isFinite(budget) || budget <= 0) return false;
  const line = Number.isFinite(fraction) ? fraction : 0.8;
  return tokens > budget * line;
}

export function summarizePrompt(prev: string, newTurns: readonly ContextTurn[], cap: number): PromptMessage[] {
  const limit = Math.max(1, Math.floor(cap));
  const lines = newTurns
    .map((turn) => `${turn.speaker}: ${turn.text.trim()}`)
    .filter((line) => line.trim().length > 2);
  return [
    {
      role: "system",
      content: `Update these notes in ${limit} tokens or fewer. Keep facts and grudges. Drop chatter. Do not invent anything.`,
    },
    {
      role: "user",
      content: `Previous notes:\n${prev.trim() || "(none)"}\n\nNew lines:\n${lines.join("\n") || "(none)"}`,
    },
  ];
}

/** No model. Keeps the newest lines and trims from the front until `cap`. */
export function fallbackSummarize(prev: string, newTurns: readonly ContextTurn[], cap: number): string {
  const lines: string[] = [];
  const prior = prev.trim();
  if (prior.length > 0) {
    for (const line of prior.split("\n")) {
      const trimmed = line.trim();
      if (trimmed.length > 0) lines.push(trimmed);
    }
  }
  for (const turn of newTurns) {
    const text = turn.text.trim();
    if (text.length === 0) continue;
    lines.push(`${turn.speaker}: ${text}`);
  }
  while (lines.length > 1 && estimateTokens(lines.join("\n")) > cap) lines.shift();
  let text = lines.join("\n");
  while (text.length > 0 && estimateTokens(text) > cap) {
    text = text.slice(Math.min(24, text.length)).trimStart();
  }
  return text;
}

export function turnTokens(turns: readonly ContextTurn[]): number {
  return turns.reduce((sum, turn) => sum + estimateTokens(`${turn.speaker}: ${turn.text}`), 0);
}

export function windowTokens(
  window: DinoWindow,
  relationship?: RelationshipMemory | null,
  elderRecord = "",
): number {
  return estimateTokens(window.memory.summary)
    + estimateTokens(window.worldNote)
    + estimateTokens(relationship?.summary ?? "")
    + estimateTokens(elderRecord)
    + turnTokens(window.turns);
}

function fitTurnText(speaker: string, text: string, cap: number): string {
  let next = text.trim();
  while (next.length > 0 && estimateTokens(`${speaker}: ${next}`) > cap) {
    next = next.slice(0, Math.max(0, Math.floor(next.length * 0.75))).trim();
  }
  return next;
}

export function pushTurn(
  window: DinoWindow,
  turn: ContextTurn,
  worldNote: string,
  split: BudgetSplit,
): { window: DinoWindow; evicted: ContextTurn[] } {
  const cap = Math.max(1, split.recentTurns);
  const turns = [...window.turns, { speaker: turn.speaker, text: turn.text, role: turn.role }];
  const evicted: ContextTurn[] = [];
  while (turns.length > 1 && turnTokens(turns) > cap) {
    const oldest = turns.shift();
    if (oldest) evicted.push(oldest);
  }
  const only = turns[0];
  if (only && turnTokens(turns) > cap) {
    turns[0] = { ...only, text: fitTurnText(only.speaker, only.text, cap) };
  }
  const pending = [...window.pending, turns.at(-1) ?? turn].slice(-PENDING_CAP);
  const countThis = turn.role === "system" ? 0 : 1;
  return {
    window: {
      ...window,
      budget: split.total,
      worldNote: clampSummary(worldNote, Math.max(1, split.world)),
      turns,
      pending,
      memory: {
        ...window.memory,
        turnsSinceSummary: window.memory.turnsSinceSummary + countThis,
      },
    },
    evicted,
  };
}

export function resizeWindow(window: DinoWindow, budget: number, replyReserve: number, elder: boolean): DinoWindow {
  const split = budgetSplit(budget, replyReserve, elder);
  const worldNote = clampSummary(window.worldNote, Math.max(1, split.world));
  if (window.turns.length === 0) return { ...window, budget: split.total, worldNote };
  const cap = Math.max(1, split.recentTurns);
  const turns = window.turns.map((turn) => ({ ...turn }));
  while (turns.length > 1 && turnTokens(turns) > cap) turns.shift();
  const only = turns[0];
  if (only && turnTokens(turns) > cap) {
    turns[0] = { ...only, text: fitTurnText(only.speaker, only.text, cap) };
  }
  return { ...window, budget: split.total, worldNote, turns };
}

export function foldSummary(args: {
  memory: DinoMemory;
  relationship: RelationshipMemory;
  turns: readonly ContextTurn[];
  now: string;
  dinoCap: number;
  relationshipCap: number;
  modelSummary?: string | null;
}): { memory: DinoMemory; relationship: RelationshipMemory } {
  const model = args.modelSummary?.trim() ?? "";
  const dinoSummary = clampSummary(
    model.length > 0 ? model : fallbackSummarize(args.memory.summary, args.turns, args.dinoCap),
    args.dinoCap,
  );
  const relationshipSummary = clampSummary(
    fallbackSummarize(args.relationship.summary, args.turns, args.relationshipCap),
    args.relationshipCap,
  );
  return {
    memory: {
      ...args.memory,
      summary: dinoSummary,
      updatedAt: args.now,
      turnsSinceSummary: 0,
    },
    relationship: {
      ...args.relationship,
      summary: relationshipSummary,
      lastSeenAt: args.now,
      interactions: args.relationship.interactions + args.turns.length,
    },
  };
}

export function applyAffinity(memory: RelationshipMemory, delta: number, at: string): RelationshipMemory {
  return {
    ...memory,
    affinity: clampAffinity(memory.affinity + delta),
    lastSeenAt: at,
  };
}

export function scoreInterest(args: {
  text: string;
  topics?: readonly TopicSentiment[];
  relationshipKind?: RelationshipKind;
  affinity?: number;
  repBand?: RepBand;
}): number {
  const kind = args.relationshipKind ?? "neutral";
  let score = 5 + KIND_BONUS[kind];
  const affinity = Number.isFinite(args.affinity) ? args.affinity ?? 0 : 0;
  score += Math.trunc(affinity / 5);
  if (args.repBand === "friendly" || args.repBand === "hostile") score += 1;
  const haystack = args.text.toLowerCase();
  for (const topic of args.topics ?? []) {
    const name = topic.name.trim().toLowerCase();
    if (name.length === 0 || !haystack.includes(name)) continue;
    score += topic.sentiment === "like" ? 2 : 1;
  }
  return Math.min(10, Math.max(1, score));
}

export function decideInterest(args: {
  interest: number;
  thresholds: InterestThresholds;
  relationshipKind: RelationshipKind;
  speakerIsDino: boolean;
  speakerIsSelf: boolean;
}): InterestDecision {
  const interest = Math.min(10, Math.max(1, Math.trunc(args.interest)));
  if (args.speakerIsSelf) {
    return { interest, action: "ignore", shouldLike: false, reason: "A dino does not answer itself.", code: "self" };
  }
  if (args.relationshipKind === "ignore") {
    return { interest, action: "ignore", shouldLike: false, reason: "This speaker is marked ignore.", code: "ignore_mark" };
  }
  if (args.speakerIsDino && args.relationshipKind !== "rival" && interest < args.thresholds.replyToReplyInterest) {
    return {
      interest,
      action: "like",
      shouldLike: true,
      reason: `No reply to a reply unless rival or interest >= ${args.thresholds.replyToReplyInterest}.`,
      code: "reply_to_reply",
    };
  }
  if (interest < args.thresholds.interestMin) {
    return {
      interest,
      action: "ignore",
      shouldLike: false,
      reason: `Interest ${interest} is below ${args.thresholds.interestMin}.`,
      code: "below_min",
    };
  }
  if (interest === args.thresholds.interestMin) {
    return {
      interest,
      action: "like",
      shouldLike: true,
      reason: `Interest ${interest} equals ${args.thresholds.interestMin}, so like only.`,
      code: "at_min",
    };
  }
  return {
    interest,
    action: "reply",
    shouldLike: false,
    reason: `Interest ${interest} is above ${args.thresholds.interestMin}.`,
    code: "above_min",
  };
}

/**
 * Direct and event chats still speak when the quota allows.
 * All-chat obeys the interest gate, including no-reply-to-reply.
 */
export function planSpeak(args: {
  mode: LabChatMode;
  quotaOk: boolean;
  quotaReason: string;
  decision: InterestDecision;
}): SpeakPlan {
  if (!args.quotaOk) {
    return {
      action: "skip",
      logAction: "skip_quota",
      reason: args.quotaReason,
      interest: args.decision.interest,
    };
  }
  if (args.mode !== "all-chat") {
    return {
      action: "reply",
      logAction: "reply",
      reason: "1:1 and game events keep the classic reply. Interest is logged beside it.",
      interest: args.decision.interest,
    };
  }
  if (args.decision.code === "reply_to_reply") {
    return {
      action: "like",
      logAction: "skip_reply_to_reply",
      reason: args.decision.reason,
      interest: args.decision.interest,
    };
  }
  if (args.decision.action === "reply") {
    return { action: "reply", logAction: "reply", reason: args.decision.reason, interest: args.decision.interest };
  }
  if (args.decision.action === "like") {
    return { action: "like", logAction: "like", reason: args.decision.reason, interest: args.decision.interest };
  }
  return { action: "skip", logAction: "ignore", reason: args.decision.reason, interest: args.decision.interest };
}

export function decideContentAction(args: {
  interest: number;
  thresholds: InterestThresholds;
  kind: "comment" | "post";
  speakerIsDino: boolean;
  speakerIsSelf: boolean;
  relationshipKind: RelationshipKind;
}): ContentDecision {
  const interest = Math.min(10, Math.max(1, Math.trunc(args.interest)));
  if (args.kind === "post") {
    if (interest >= args.thresholds.postInterestMin) {
      return {
        action: "POST",
        interest,
        shouldLike: false,
        reason: `Interest ${interest} meets the post minimum ${args.thresholds.postInterestMin}.`,
      };
    }
    return {
      action: "NONE",
      interest,
      shouldLike: false,
      reason: `Interest ${interest} is under the post minimum ${args.thresholds.postInterestMin}.`,
    };
  }
  const comment = decideInterest({
    interest,
    thresholds: args.thresholds,
    relationshipKind: args.relationshipKind,
    speakerIsDino: args.speakerIsDino,
    speakerIsSelf: args.speakerIsSelf,
  });
  const action: ContentAction = comment.action === "reply" ? "REPLY" : comment.action === "like" ? "LIKE" : "IGNORE";
  return { action, interest: comment.interest, reason: comment.reason, shouldLike: comment.shouldLike };
}

export function canSpeak(args: {
  dinoId: string;
  subjectKey: string;
  log: readonly QuotaLogEntry[];
  now: string;
  quota: SpeechQuota;
  random?: () => number;
}): { ok: boolean; reason: string } {
  const nowMs = Date.parse(args.now);
  if (!Number.isFinite(nowMs)) return { ok: false, reason: "clock is unreadable" };
  const spoken = args.log.filter((entry) => {
    if (entry.dinoId !== args.dinoId) return false;
    if (entry.action !== "reply" && entry.action !== "post") return false;
    return Number.isFinite(Date.parse(entry.at));
  });
  const minute = spoken.filter((entry) => nowMs - Date.parse(entry.at) < 60_000 && nowMs - Date.parse(entry.at) >= 0);
  if (minute.length >= args.quota.maxPerMinute) {
    return { ok: false, reason: `per-minute cap (${minute.length}/${args.quota.maxPerMinute})` };
  }
  const hour = spoken.filter((entry) => nowMs - Date.parse(entry.at) < 3_600_000 && nowMs - Date.parse(entry.at) >= 0);
  if (hour.length >= args.quota.maxPerHour) {
    return { ok: false, reason: `per-hour cap (${hour.length}/${args.quota.maxPerHour})` };
  }
  const last = spoken.reduce<number | null>((best, entry) => {
    const at = Date.parse(entry.at);
    return best === null || at > best ? at : best;
  }, null);
  if (last !== null && args.quota.restSeconds > 0) {
    const leftMs = args.quota.restSeconds * 1000 - (nowMs - last);
    if (leftMs > 0) return { ok: false, reason: `resting (${Math.ceil(leftMs / 1000)}s left)` };
  }
  const same = hour.filter((entry) => entry.subjectKey === args.subjectKey);
  if (same.length >= args.quota.maxToSamePlayerPerHour) {
    return { ok: false, reason: `same-player cap (${same.length}/${args.quota.maxToSamePlayerPerHour})` };
  }
  if (args.quota.speakChance <= 0) return { ok: false, reason: "speak chance" };
  if (args.quota.speakChance < 1) {
    const roll = args.random ? args.random() : Math.random();
    if (roll >= args.quota.speakChance) return { ok: false, reason: "speak chance" };
  }
  return { ok: true, reason: "ok" };
}

export function prioritize<T>(items: readonly T[], kindOf: (item: T) => RelationshipKind): T[] {
  return items
    .map((item, index) => ({ item, index, kind: kindOf(item) }))
    .filter((row) => row.kind !== "ignore")
    .sort((a, b) => (a.kind === b.kind ? a.index - b.index : PRIORITY[a.kind] - PRIORITY[b.kind]))
    .map((row) => row.item);
}

export function relationshipTag(name: string): string {
  const clean = name.trim().replace(/[<>\s]+/g, "_").replace(/_+/g, "_").replace(/^_|_$/g, "");
  return `you_and_${clean.length > 0 ? clean : "them"}`;
}

export interface AssembleTurnInput {
  personaName: string;
  personaBlock: string;
  worldNote: string;
  dinoSummary: string;
  relationshipSummary: string;
  subjectName: string;
  elder: boolean;
  playerRecord: string;
  recentTurns: readonly ContextTurn[];
  budget: number;
  replyReserve: number;
}

export function assembleTurn(input: AssembleTurnInput): PromptMessage[] {
  const split = budgetSplit(input.budget, input.replyReserve, input.elder);
  const persona = clampSummary(input.personaBlock, split.persona);
  const world = clampSummary(input.worldNote, split.world);
  const memory = clampSummary(input.dinoSummary, split.dinoSummary);
  const relation = clampSummary(input.relationshipSummary, split.relationship);
  const record = input.elder ? clampSummary(input.playerRecord, split.elderStats) : "";
  const blocks = [
    persona,
    memory.length > 0 ? `<your_memory>\n${memory}` : "",
    relation.length > 0 ? `<${relationshipTag(input.subjectName)}>\n${relation}` : "",
    record.length > 0 ? `<player_record>\n${record}` : "",
    world.length > 0 ? `<world>\n${world}` : "",
  ].filter((part) => part.length > 0);
  const system: PromptMessage = { role: "system", content: blocks.join("\n") };
  const history: PromptMessage[] = [];
  for (const turn of input.recentTurns) {
    if (turn.role === "system") continue;
    const text = turn.text.trim();
    if (text.length === 0) continue;
    if (turn.role === "dino" && turn.speaker === input.personaName) {
      history.push({ role: "assistant", content: text });
    } else if (turn.role === "user") {
      history.push({ role: "user", content: text });
    } else {
      history.push({ role: "user", content: `${turn.speaker}: ${text}` });
    }
  }
  return trimPromptToBudget([system, ...history], split.total, split.replyReserve);
}

/** Drop oldest history, then shrink text, until the prompt leaves `replyReserve` free. */
export function trimPromptToBudget(
  messages: readonly PromptMessage[],
  budget: number,
  replyReserve = 0,
): PromptMessage[] {
  const total = clampBudget(budget);
  const reserve = Math.min(Math.max(0, Math.floor(replyReserve)), Math.max(0, total - 1));
  const limit = Math.max(1, total - reserve);
  const next = messages.map((message) => ({ ...message }));
  while (next.length > 2 && messageTokenEstimate(next) > limit) next.splice(1, 1);
  let guard = 0;
  while (messageTokenEstimate(next) > limit && guard < 10_000) {
    guard += 1;
    const system = next[0];
    const last = next[next.length - 1];
    const target = system && estimateTokens(system.content) > 0 ? system : last;
    if (!target || target.content.length === 0) break;
    const cut = Math.max(1, Math.floor(target.content.length * 0.15));
    target.content = target.content.slice(0, Math.max(0, target.content.length - cut)).trimEnd();
  }
  return next.filter((message) => message.content.length > 0);
}
