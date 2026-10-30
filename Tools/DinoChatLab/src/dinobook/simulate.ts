import { estimateTokens } from "../logic/context";
import { emptyBrief } from "./corePort";
import { acceptSpeech, clampToTokens, withReference } from "./text";
import {
  bragLine,
  complainLine,
  jobLine,
  mineralLine,
  payAll,
  paydayDemand,
  paydaySeats,
  seedWageCards,
  wantedFactory,
} from "./wages";
import {
  assignRoles,
  briefFor,
  chooseApproach,
  demandLine,
  elderReport,
  followUpChat,
  grumbleChatLine,
  leadLine,
  pickLeader,
  planFor,
  roleLine,
  verdictLine,
  type Assignment,
} from "./plans";
import {
  BIG_GRUMBLE_ID,
  eldersOf,
  familyOf,
  friendsOf,
  LAB_TREE,
  type FamilyTree,
} from "./tree";
import {
  DEFAULT_CHAT_BUDGET,
  DEFAULT_POLICY,
  POST_TOKEN_CAP,
  type AllChatLine,
  type ChatBudget,
  type DinobookPost,
  type FamilyBrief,
  type PlanId,
  type PostDecision,
  type PostLink,
  type RoleId,
  type RosterDino,
  type RunInput,
  type RunResult,
  type WageCard,
  type Skip,
  type SpamPolicy,
} from "./types";

const SPEAK_FLOOR = 5;

function playerLabel(name: string): string {
  const trimmed = name.trim();
  return trimmed.length > 0 ? trimmed : "you";
}

function policyOf(partial: Partial<SpamPolicy> | undefined): SpamPolicy {
  return { ...DEFAULT_POLICY, ...partial };
}

function chatOf(partial: Partial<ChatBudget> | undefined): ChatBudget {
  const chat = { ...DEFAULT_CHAT_BUDGET, ...partial };
  return {
    maxLines: Math.max(0, Math.floor(chat.maxLines)),
    maxTokens: Math.max(0, Math.floor(chat.maxTokens)),
    leaderOnly: chat.leaderOnly,
  };
}

function dinoById(roster: readonly RosterDino[], id: string): RosterDino | null {
  return roster.find((dino) => dino.id === id) ?? null;
}

interface Draft {
  dino: RosterDino;
  role: RoleId;
  interest: number;
  reason: string;
  text: string;
  link: PostLink;
  parentId: string | null;
  familyId: string | null;
}

export function runDinobookEvent(input: RunInput, tree: FamilyTree = LAB_TREE): RunResult {
  const policy = policyOf(input.policy);
  const chat = chatOf(input.chat);
  const at = input.at ?? new Date().toISOString();
  const player = playerLabel(input.playerName);
  const memory: Record<string, string> = { ...(input.memory ?? {}) };
  const prior = [...(input.priorTexts ?? [])].slice(-policy.dedupeWindow);
  const wageCards = (input.wages ?? seedWageCards()).map((card) => ({ ...card }));
  let nextId = input.idStart ?? 0;
  const posts: DinobookPost[] = [];
  const decisions: PostDecision[] = [];
  const skipped: Skip[] = [];
  const speakers = new Set<string>();

  const id = () => {
    nextId += 1;
    return `db-${nextId}`;
  };

  const remember = (dinoId: string, text: string) => {
    memory[dinoId] = clampToTokens(text, 40);
  };

  const pushPost = (draft: Draft): DinobookPost | null => {
    if (draft.interest < SPEAK_FLOOR) {
      decisions.push({
        dinoId: draft.dino.id,
        name: draft.dino.name,
        action: "NONE",
        interest: draft.interest,
        reason: "interest below the speak floor",
        content: "",
      });
      skipped.push({ dinoId: draft.dino.id, name: draft.dino.name, reason: "interest" });
      return null;
    }
    const postsAlready = posts.filter((post) => post.authorId === draft.dino.id).length;
    const extra = policy.summonerExtraPost && draft.dino.id === BIG_GRUMBLE_ID ? 1 : 0;
    if (postsAlready >= policy.maxPostsPerSpeaker + extra) {
      skipped.push({ dinoId: draft.dino.id, name: draft.dino.name, reason: "speaker-cap" });
      decisions.push({
        dinoId: draft.dino.id,
        name: draft.dino.name,
        action: "NONE",
        interest: draft.interest,
        reason: "that dino already posted",
        content: "",
      });
      return null;
    }
    if (!speakers.has(draft.dino.id) && speakers.size >= policy.maxSpeakersPerEvent) {
      skipped.push({ dinoId: draft.dino.id, name: draft.dino.name, reason: "speaker-cap" });
      decisions.push({
        dinoId: draft.dino.id,
        name: draft.dino.name,
        action: "NONE",
        interest: draft.interest,
        reason: "speaker cap for this event",
        content: "",
      });
      return null;
    }
    const priorSpeaker = posts.length > 0 ? posts[posts.length - 1]!.authorName : null;
    const spoken = posts.length > 0 ? withReference(draft.text, priorSpeaker) : draft.text;
    const accepted = acceptSpeech(spoken, prior, priorSpeaker, POST_TOKEN_CAP);
    if (!accepted) {
      skipped.push({ dinoId: draft.dino.id, name: draft.dino.name, reason: "duplicate" });
      decisions.push({
        dinoId: draft.dino.id,
        name: draft.dino.name,
        action: "NONE",
        interest: draft.interest,
        reason: "duplicate line",
        content: draft.text,
      });
      return null;
    }
    prior.push(accepted.text);
    speakers.add(draft.dino.id);
    const post: DinobookPost = {
      id: id(),
      at,
      eventId: input.event,
      authorId: draft.dino.id,
      authorName: draft.dino.name,
      color: draft.dino.color,
      familyId: draft.familyId,
      role: draft.role,
      link: draft.parentId ? draft.link : "root",
      parentId: draft.parentId,
      content: accepted.text,
      tokens: accepted.tokens,
      action: draft.parentId ? "REPLY" : "POST",
      interest: draft.interest,
      reason: draft.reason,
      filtered: accepted.filtered,
    };
    posts.push(post);
    decisions.push({
      dinoId: draft.dino.id,
      name: draft.dino.name,
      action: post.action,
      interest: draft.interest,
      reason: draft.reason,
      content: accepted.text,
    });
    remember(draft.dino.id, accepted.text);
    return post;
  };

  if (input.event === "payday") {
    return runPayday({
      input, tree, policy, chat, at, player, memory, prior, posts, decisions, skipped, pushPost, id,
      nextId: () => nextId,
      wageCards,
    });
  }

  if (input.event === "summon-big-grumble") {
    return runPoll({
      input, tree, policy, chat, at, player, memory, prior, posts, decisions, skipped, pushPost, id, nextId: () => nextId,
    });
  }

  const leaderId = pickLeader(input.event, tree, input.roster, input.briefs);
  const leader = dinoById(input.roster, leaderId);
  const plan: PlanId = planFor(input.event, leader);
  if (!leader) {
    return emptyResult(plan, leaderId, memory, nextId, wageCards);
  }
  const family = familyOf(tree, leader.id);
  const assignments = assignRoles(plan, leader.id, tree, input.roster, input.briefs);
  const seated = assignments.slice(0, Math.max(0, policy.maxSpeakersPerEvent));
  for (const extra of assignments.slice(seated.length)) {
    const dino = dinoById(input.roster, extra.dinoId);
    if (!dino) continue;
    skipped.push({ dinoId: dino.id, name: dino.name, reason: "speaker-cap" });
    decisions.push({
      dinoId: dino.id,
      name: dino.name,
      action: "NONE",
      interest: extra.interest,
      reason: "speaker cap for this event",
      content: "",
    });
  }

  const published: { assignment: Assignment; post: DinobookPost }[] = [];
  for (const assignment of seated) {
    const dino = dinoById(input.roster, assignment.dinoId);
    if (!dino) continue;
    const home = familyOf(tree, dino.id);
    const link = linkFor(assignment, posts, tree);
    const priorName = posts.length > 0 ? posts[posts.length - 1]!.authorName : leader.name;
    const text = assignment.role === "lead"
      ? leadLine({
        leader: dino,
        plan,
        event: input.event,
        player,
        familyName: home?.name ?? family?.name ?? "the family",
        roster: input.roster,
        assignments,
        memory: memory[dino.id] ?? "",
      })
      : roleLine({
        dino,
        role: assignment.role,
        priorName,
        player,
        familyName: home?.name ?? "Their family",
      });
    const post = pushPost({
      dino,
      role: assignment.role,
      interest: assignment.interest,
      reason: reasonFor(assignment.role, plan, link.link),
      text,
      link: link.link,
      parentId: link.parentId,
      familyId: home?.familyId ?? null,
    });
    if (post) published.push({ assignment, post });
  }

  const allChat = speakAllChat({
    chat,
    at,
    eventId: input.event,
    player,
    plan,
    leader,
    roster: input.roster,
    posts,
    prior,
    id,
  });

  return {
    posts,
    allChat,
    decisions,
    skipped,
    planId: plan,
    leaderId: leader.id,
    approach: null,
    approachReason: "",
    nextId,
    memory,
    wages: wageCards,
  };
}

function reasonFor(role: RoleId, plan: PlanId, link: PostLink): string {
  if (role === "lead") return `elder plan: ${plan}`;
  if (link === "side") return `side link, role ${role}`;
  if (link === "child") return `family role: ${role}`;
  return `role ${role} on ${plan}`;
}

function linkFor(
  assignment: Assignment,
  posts: readonly DinobookPost[],
  tree: FamilyTree,
): { link: PostLink; parentId: string | null } {
  if (assignment.role === "greet" || assignment.role === "job-seeking") {
    const friendPost = posts.find((post) => friendsOf(tree, assignment.dinoId).includes(post.authorId));
    if (friendPost) return { link: "side", parentId: friendPost.id };
  }
  const home = familyOf(tree, assignment.dinoId);
  if (home && home.elderId !== assignment.dinoId) {
    const elderPost = posts.find((post) => post.authorId === home.elderId);
    if (elderPost) return { link: "child", parentId: elderPost.id };
  }
  if (assignment.role !== "lead" && assignment.role !== "warn") {
    const friendPost = posts.find((post) => friendsOf(tree, assignment.dinoId).includes(post.authorId));
    if (friendPost) return { link: "side", parentId: friendPost.id };
  }
  return { link: "root", parentId: null };
}

function speakAllChat(args: {
  chat: ChatBudget;
  at: string;
  eventId: RunInput["event"];
  player: string;
  plan: PlanId;
  leader: RosterDino;
  roster: readonly RosterDino[];
  posts: readonly DinobookPost[];
  prior: string[];
  id: () => string;
}): AllChatLine[] {
  if (args.chat.maxLines <= 0 || args.chat.maxTokens <= 0) return [];
  const lines: AllChatLine[] = [];
  let tokensLeft = args.chat.maxTokens;
  const seen = [...args.prior];

  const add = (speaker: RosterDino, draft: string, reason: string) => {
    if (lines.length >= args.chat.maxLines || tokensLeft <= 0) return;
    const priorSpeaker = lines.length > 0
      ? lines[lines.length - 1]!.speakerName
      : (args.posts.length > 0 ? args.posts[args.posts.length - 1]!.authorName : null);
    const accepted = acceptSpeech(draft, seen, priorSpeaker, Math.min(POST_TOKEN_CAP, tokensLeft));
    if (!accepted) return;
    seen.push(accepted.text);
    tokensLeft -= accepted.tokens;
    lines.push({
      id: args.id(),
      at: args.at,
      eventId: args.eventId,
      speakerId: speaker.id,
      speakerName: speaker.name,
      color: speaker.color,
      text: accepted.text,
      tokens: accepted.tokens,
      reason,
      filtered: accepted.filtered,
    });
  };

  add(args.leader, demandLine(args.leader, args.plan, args.player), "leader demand");
  if (!args.chat.leaderOnly) {
    for (const post of args.posts) {
      if (lines.length >= args.chat.maxLines) break;
      if (post.authorId === args.leader.id) continue;
      const speaker = args.roster.find((dino) => dino.id === post.authorId);
      if (!speaker) continue;
      const priorName = lines[lines.length - 1]?.speakerName ?? args.leader.name;
      add(speaker, followUpChat(speaker, priorName, args.player), "turn after the leader");
    }
  }
  return lines;
}

function runPoll(ctx: {
  input: RunInput;
  tree: FamilyTree;
  policy: SpamPolicy;
  chat: ChatBudget;
  at: string;
  player: string;
  memory: Record<string, string>;
  prior: string[];
  posts: DinobookPost[];
  decisions: PostDecision[];
  skipped: Skip[];
  pushPost: (draft: Draft) => DinobookPost | null;
  id: () => string;
  nextId: () => number;
}): RunResult {
  const grumble = dinoById(ctx.input.roster, BIG_GRUMBLE_ID) ?? {
    id: BIG_GRUMBLE_ID,
    name: "Big Grumble",
    alignment: "CN" as const,
    color: "#c4512c",
  };
  const poll = ctx.pushPost({
    dino: grumble,
    role: "lead",
    interest: 9,
    reason: "poll elders before moving",
    text: `Huh. Elders, report on ${ctx.player} before I move. I want rep, kills, bounties, and rentals.`,
    link: "root",
    parentId: null,
    familyId: null,
  });

  const families = eldersOf(ctx.tree);
  const replies: { name: string; familyName: string; brief: FamilyBrief }[] = [];
  for (const family of families) {
    const elder = dinoById(ctx.input.roster, family.elderId);
    if (!elder) continue;
    const brief = briefFor(ctx.input.briefs, family.familyId) ?? emptyBrief(family.familyId);
    const priorName = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : grumble.name;
    const post = ctx.pushPost({
      dino: elder,
      role: "report",
      interest: 8,
      reason: `elder report for ${family.name}`,
      text: elderReport({ elder, familyName: family.name, brief, priorName, player: ctx.player }),
      link: "child",
      parentId: poll?.id ?? null,
      familyId: family.familyId,
    });
    if (post) replies.push({ name: elder.name, familyName: family.name, brief });
  }

  const heard = replies.map((reply) => reply.brief);
  const choice = chooseApproach(heard.length > 0 ? heard : ctx.input.briefs);
  ctx.pushPost({
    dino: grumble,
    role: "verdict",
    interest: 9,
    reason: `verdict: ${choice.id}`,
    text: verdictLine({ player: ctx.player, replies, choice }),
    link: "child",
    parentId: poll?.id ?? null,
    familyId: null,
  });

  const allChat = ctx.chat.maxLines > 0 && ctx.chat.maxTokens > 0
    ? speakGrumble(ctx, grumble, choice.id)
    : [];

  return {
    posts: ctx.posts,
    allChat,
    decisions: ctx.decisions,
    skipped: ctx.skipped,
    planId: "poll",
    leaderId: grumble.id,
    approach: choice.id,
    approachReason: choice.reason,
    nextId: ctx.nextId(),
    memory: ctx.memory,
    wages: ctx.input.wages ? ctx.input.wages.map((card) => ({ ...card })) : seedWageCards(),
  };
}

function runPayday(ctx: {
  input: RunInput;
  tree: FamilyTree;
  policy: SpamPolicy;
  chat: ChatBudget;
  at: string;
  player: string;
  memory: Record<string, string>;
  prior: string[];
  posts: DinobookPost[];
  decisions: PostDecision[];
  skipped: Skip[];
  pushPost: (draft: Draft) => DinobookPost | null;
  id: () => string;
  nextId: () => number;
  wageCards: WageCard[];
}): RunResult {
  const paid = payAll(ctx.wageCards);
  const opening = wantedFactory(paid);
  const seats = paydaySeats(paid, ctx.input.roster, ctx.tree);
  const seated = seats.slice(0, Math.max(0, ctx.policy.maxSpeakersPerEvent));
  for (const extra of seats.slice(seated.length)) {
    const dino = dinoById(ctx.input.roster, extra.card.dinoId);
    if (!dino) continue;
    ctx.skipped.push({ dinoId: dino.id, name: dino.name, reason: "speaker-cap" });
    ctx.decisions.push({
      dinoId: dino.id,
      name: dino.name,
      action: "NONE",
      interest: extra.interest,
      reason: "speaker cap for this event",
      content: "",
    });
  }

  let leaderId = seated[0]?.card.dinoId ?? "";
  let leaderCard = seated[0]?.card ?? paid[0];
  for (const seat of seated) {
    const dino = dinoById(ctx.input.roster, seat.card.dinoId);
    if (!dino) continue;
    const home = familyOf(ctx.tree, dino.id);
    const link = linkFor({ dinoId: dino.id, role: seat.role, interest: seat.interest }, ctx.posts, ctx.tree);
    const priorName = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : "";
    const text = seat.role === "brag-pay"
      ? bragLine(dino.id, seat.card)
      : seat.role === "mineral-find"
        ? mineralLine(dino.id, seat.card, priorName || dino.name)
        : seat.role === "complain-work"
          ? complainLine(dino.id, seat.card, priorName || dino.name)
          : jobLine(dino.id, priorName, opening.name);
    const post = ctx.pushPost({
      dino,
      role: seat.role,
      interest: seat.interest,
      reason: `post type: ${seat.role}`,
      text,
      link: link.link,
      parentId: link.parentId,
      familyId: home?.familyId ?? null,
    });
    if (post && seat.role === "mineral-find") seat.card.mineralsFound += 1;
    if (post && seat.role === "brag-pay") {
      leaderId = dino.id;
      leaderCard = seat.card;
    }
    if (post && !leaderId) leaderId = dino.id;
  }

  const leader = dinoById(ctx.input.roster, leaderId);
  const allChat = leader && leaderCard && ctx.chat.maxLines > 0 && ctx.chat.maxTokens > 0
    ? speakPayday(ctx, leader, paydayDemand(leaderCard, opening))
    : [];

  return {
    posts: ctx.posts,
    allChat,
    decisions: ctx.decisions,
    skipped: ctx.skipped,
    planId: "payday",
    leaderId: leader?.id ?? leaderId,
    approach: null,
    approachReason: "",
    nextId: ctx.nextId(),
    memory: ctx.memory,
    wages: paid,
  };
}

function speakPayday(
  ctx: {
    chat: ChatBudget;
    at: string;
    input: RunInput;
    posts: DinobookPost[];
    prior: string[];
    id: () => string;
  },
  leader: RosterDino,
  line: string,
): AllChatLine[] {
  const priorSpeaker = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : null;
  const accepted = acceptSpeech(line, ctx.prior, priorSpeaker, Math.min(POST_TOKEN_CAP, ctx.chat.maxTokens));
  if (!accepted) return [];
  return [{
    id: ctx.id(),
    at: ctx.at,
    eventId: ctx.input.event,
    speakerId: leader.id,
    speakerName: leader.name,
    color: leader.color,
    text: accepted.text,
    tokens: accepted.tokens,
    reason: "leader payday",
    filtered: accepted.filtered,
  }];
}

function speakGrumble(
  ctx: {
    chat: ChatBudget;
    at: string;
    input: RunInput;
    posts: DinobookPost[];
    prior: string[];
    id: () => string;
    player: string;
  },
  grumble: RosterDino,
  approach: NonNullable<RunResult["approach"]>,
): AllChatLine[] {
  if (ctx.chat.maxLines <= 0 || ctx.chat.maxTokens <= 0) return [];
  const priorSpeaker = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : null;
  const accepted = acceptSpeech(
    grumbleChatLine(approach, ctx.player),
    ctx.prior,
    priorSpeaker,
    Math.min(POST_TOKEN_CAP, ctx.chat.maxTokens),
  );
  if (!accepted) return [];
  return [{
    id: ctx.id(),
    at: ctx.at,
    eventId: ctx.input.event,
    speakerId: grumble.id,
    speakerName: grumble.name,
    color: grumble.color,
    text: accepted.text,
    tokens: accepted.tokens,
    reason: `leader demand: ${approach}`,
    filtered: accepted.filtered,
  }];
}

function emptyResult(
  plan: PlanId,
  leaderId: string,
  memory: Record<string, string>,
  nextId: number,
  wages: WageCard[],
): RunResult {
  return {
    posts: [],
    allChat: [],
    decisions: [],
    skipped: [],
    planId: plan,
    leaderId,
    approach: null,
    approachReason: "",
    nextId,
    memory,
    wages,
  };
}

export function postTokensOk(posts: readonly { content: string }[], cap = POST_TOKEN_CAP): boolean {
  return posts.every((post) => estimateTokens(post.content) <= cap);
}
