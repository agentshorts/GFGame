import { estimateTokens } from "../logic/context";
import { DINOBOOK_EVENTS } from "./types";
import { idleLlm, nextTraceId, roundMs, templateCandidates } from "../logic/trace";
import type { LlmTrace, TraceAttribution, TraceDecision, TraceEvent, TraceKind } from "../logic/traceTypes";
import { emptyBrief } from "./corePort";
import { acceptSpeech } from "./text";
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
  creditReason,
  creditSpeaker,
  demandLine,
  elderReport,
  followUpChat,
  grumbleChatLine,
  leadLine,
  leaderReason,
  memoryNote,
  NOTE_TOKEN_CAP,
  planFor,
  roleLine,
  verdictLine,
  voiceTag,
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
  type DinobookEventId,
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
  utterance: string;
  attribution: TraceAttribution;
  decisions: TraceDecision[];
  memory: string;
}

interface TraceBag {
  runId: string;
  kind: TraceKind;
  eventId: DinobookEventId;
  eventLabel: string;
  at: string;
  traces: TraceEvent[];
}

function eventLabelOf(event: DinobookEventId): string {
  return DINOBOOK_EVENTS.find((row) => row.id === event)?.label ?? event;
}

function kindFor(event: DinobookEventId): TraceKind {
  if (event === "payday") return "payday";
  if (event === "summon-big-grumble") return "elder";
  return "dinobook";
}

function feedContext(eventLabel: string, plan: string, player: string, posts: readonly DinobookPost[]): string {
  const lines = [
    `Event ${eventLabel}.`,
    plan ? `Plan ${plan}.` : "",
    `Player ${player}.`,
    ...posts.map((post) => `${post.authorName}: ${post.content}`),
  ].filter((line) => line.length > 0);
  return lines.join("\n");
}

function postLlm(dino: RosterDino, role: string, memory: string, context: string): LlmTrace {
  return idleLlm({
    persona: `${dino.name}. Lab alignment ${dino.alignment}. Voice ${voiceTag(dino.id)} Role ${role}.`,
    memory,
    context,
  });
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
  const bag: TraceBag = {
    runId: input.runId ?? `run-${(input.idStart ?? 0) + 1}-${input.event}`,
    kind: kindFor(input.event),
    eventId: input.event,
    eventLabel: eventLabelOf(input.event),
    at,
    traces: [],
  };

  const id = () => {
    nextId += 1;
    return `db-${nextId}`;
  };

  const remember = (dinoId: string, text: string) => {
    memory[dinoId] = memoryNote(text, NOTE_TOKEN_CAP);
  };

  let planLabel = "";
  const stamp = (draft: Draft, post: DinobookPost, ms: number) => {
    const previous = posts.length > 1 ? posts[posts.length - 2]!.authorName : null;
    const llm = postLlm(draft.dino, draft.role, draft.memory, feedContext(bag.eventLabel, planLabel, player, posts));
    const traceId = nextTraceId();
    post.traceId = traceId;
    bag.traces.push({
      id: traceId,
      at,
      kind: bag.kind,
      dinoId: draft.dino.id,
      dinoName: draft.dino.name,
      runId: bag.runId,
      eventId: bag.eventId,
      eventLabel: bag.eventLabel,
      input: draft.utterance,
      finalText: post.content,
      chosenSource: "canned",
      reason: `${draft.reason} ${creditReason(draft.attribution, previous)}`.trim(),
      candidates: templateCandidates(post.content, ms),
      llm,
      decisions: [
        ...draft.decisions,
        {
          stage: "pick",
          chosen: "canned",
          reason: "The feed keeps the template. Classic engines are probed beside it when Trace is on.",
        },
        {
          stage: "filter",
          chosen: post.content,
          reason: post.filtered ? "PG filter changed the template." : "PG filter left the template alone.",
          detail: { filtered: post.filtered },
        },
      ],
      timings: { template: ms },
      attribution: draft.attribution,
    });
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
    const started = performance.now();
    const accepted = acceptSpeech(draft.text, prior, priorSpeaker, POST_TOKEN_CAP);
    const ms = roundMs(performance.now() - started);
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
      runId: bag.runId,
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
    stamp(draft, post, ms);
    return post;
  };

  if (input.event === "payday") {
    planLabel = "payday";
    return runPayday({
      input, tree, policy, chat, at, player, memory, prior, posts, decisions, skipped, pushPost, id,
      nextId: () => nextId,
      wageCards,
      bag,
    });
  }

  if (input.event === "summon-big-grumble") {
    planLabel = "poll";
    return runPoll({
      input, tree, policy, chat, at, player, memory, prior, posts, decisions, skipped, pushPost, id, nextId: () => nextId,
      bag,
    });
  }

  const picked = leaderReason(input.event, tree, input.roster, input.briefs);
  const leaderId = picked.id;
  const leader = dinoById(input.roster, leaderId);
  const plan: PlanId = planFor(input.event, leader);
  bag.traces.push({
    id: nextTraceId(),
    at,
    kind: "elder",
    dinoId: leaderId,
    dinoName: picked.name,
    runId: bag.runId,
    eventId: input.event,
    eventLabel: bag.eventLabel,
    input: input.event,
    finalText: picked.reason,
    chosenSource: "alignment",
    reason: picked.reason,
    candidates: [{ source: "alignment", text: picked.reason, ms: 0, note: "elder pick" }],
    llm: null,
    decisions: [{ stage: "elder", chosen: leaderId, reason: picked.reason }],
    timings: { elder: 0 },
  });
  if (!leader) {
    return emptyResult(plan, leaderId, memory, nextId, wageCards, bag);
  }
  planLabel = plan;
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
    const credit = assignment.role === "lead"
      ? { name: dino.name, dinoId: dino.id, postId: null, because: "self" as const }
      : creditSpeaker({
        posts,
        parentId: link.parentId,
        selfId: dino.id,
        leaderId: leader.id,
        leaderName: leader.name,
      });
    const noted = memory[dino.id] ?? "";
    const text = assignment.role === "lead"
      ? leadLine({
        leader: dino,
        plan,
        event: input.event,
        player,
        familyName: home?.name ?? family?.name ?? "the family",
        roster: input.roster,
        assignments,
        memory: noted,
      })
      : roleLine({
        dino,
        role: assignment.role,
        priorName: credit.name,
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
      utterance: assignment.role === "lead" ? `${player} ${input.event}` : `${credit.name}: ${text}`,
      attribution: credit,
      memory: noted,
      decisions: [
        { stage: "elder", chosen: leader.id, reason: picked.reason },
        {
          stage: "present",
          reason: noted
            ? `Memory note capped at ${NOTE_TOKEN_CAP} tokens. Older Still noted text is stripped so the lead cannot snowball.`
            : "No earlier note for this dino.",
        },
        { stage: "pick", chosen: credit.name, reason: creditReason(credit, posts.at(-1)?.authorName ?? null) },
      ],
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
    bag,
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
    runId: bag.runId,
    traces: bag.traces,
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
  bag: TraceBag;
}): AllChatLine[] {
  if (args.chat.maxLines <= 0 || args.chat.maxTokens <= 0) return [];
  const lines: AllChatLine[] = [];
  let tokensLeft = args.chat.maxTokens;
  const seen = [...args.prior];

  const add = (speaker: RosterDino, draft: string, reason: string, credit: TraceAttribution) => {
    if (lines.length >= args.chat.maxLines || tokensLeft <= 0) return;
    const priorSpeaker = lines.length > 0
      ? lines[lines.length - 1]!.speakerName
      : (args.posts.length > 0 ? args.posts[args.posts.length - 1]!.authorName : null);
    const started = performance.now();
    const accepted = acceptSpeech(draft, seen, priorSpeaker, Math.min(POST_TOKEN_CAP, tokensLeft));
    const ms = roundMs(performance.now() - started);
    if (!accepted) return;
    seen.push(accepted.text);
    tokensLeft -= accepted.tokens;
    const traceId = nextTraceId();
    lines.push({
      id: args.id(),
      at: args.at,
      runId: args.bag.runId,
      eventId: args.eventId,
      speakerId: speaker.id,
      speakerName: speaker.name,
      color: speaker.color,
      text: accepted.text,
      tokens: accepted.tokens,
      reason,
      filtered: accepted.filtered,
      traceId,
    });
    args.bag.traces.push({
      id: traceId,
      at: args.at,
      kind: args.bag.kind,
      dinoId: speaker.id,
      dinoName: speaker.name,
      runId: args.bag.runId,
      eventId: args.bag.eventId,
      eventLabel: args.bag.eventLabel,
      input: draft,
      finalText: accepted.text,
      chosenSource: "canned",
      reason: `${reason} ${creditReason(credit, priorSpeaker)}`.trim(),
      candidates: templateCandidates(accepted.text, ms),
      llm: postLlm(speaker, "all-chat", "", feedContext(args.bag.eventLabel, args.plan, args.player, args.posts)),
      decisions: [
        { stage: "pick", chosen: "canned", reason: "All-chat keeps the demand template inside the token budget." },
        { stage: "present", chosen: credit.name, reason: creditReason(credit, priorSpeaker) },
      ],
      timings: { template: ms },
      attribution: credit,
    });
  };

  add(
    args.leader,
    demandLine(args.leader, args.plan, args.player),
    "leader demand",
    { name: args.leader.name, dinoId: args.leader.id, postId: null, because: "self" },
  );
  if (!args.chat.leaderOnly) {
    for (const post of args.posts) {
      if (lines.length >= args.chat.maxLines) break;
      if (post.authorId === args.leader.id) continue;
      const speaker = args.roster.find((dino) => dino.id === post.authorId);
      if (!speaker) continue;
      const heard = lines[lines.length - 1];
      const credit: TraceAttribution = heard
        ? { name: heard.speakerName, dinoId: heard.speakerId, postId: heard.id, because: "lead" }
        : { name: args.leader.name, dinoId: args.leader.id, postId: null, because: "lead" };
      add(speaker, followUpChat(speaker, credit.name, args.player), "turn after the leader", credit);
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
  bag: TraceBag;
}): RunResult {
  const grumble = dinoById(ctx.input.roster, BIG_GRUMBLE_ID) ?? {
    id: BIG_GRUMBLE_ID,
    name: "Big Grumble",
    alignment: "CN" as const,
    color: "#c4512c",
  };
  const selfCredit: TraceAttribution = { name: grumble.name, dinoId: grumble.id, postId: null, because: "self" };
  const poll = ctx.pushPost({
    dino: grumble,
    role: "lead",
    interest: 9,
    reason: "poll elders before moving",
    text: `Huh. Elders, report on ${ctx.player} before I move. I want rep, kills, bounties, and rentals.`,
    link: "root",
    parentId: null,
    familyId: null,
    utterance: `report on ${ctx.player}`,
    attribution: selfCredit,
    memory: ctx.memory[grumble.id] ?? "",
    decisions: [{ stage: "elder", reason: "Big Grumble polls every elder before moving." }],
  });

  const families = eldersOf(ctx.tree);
  const replies: { name: string; familyName: string; brief: FamilyBrief }[] = [];
  for (const family of families) {
    const elder = dinoById(ctx.input.roster, family.elderId);
    if (!elder) continue;
    const brief = briefFor(ctx.input.briefs, family.familyId) ?? emptyBrief(family.familyId);
    const credit: TraceAttribution = poll
      ? { name: grumble.name, dinoId: grumble.id, postId: poll.id, because: "parent" }
      : { name: grumble.name, dinoId: grumble.id, postId: null, because: "lead" };
    const post = ctx.pushPost({
      dino: elder,
      role: "report",
      interest: 8,
      reason: `elder report for ${family.name}`,
      text: elderReport({ elder, familyName: family.name, brief, priorName: credit.name, player: ctx.player }),
      link: "child",
      parentId: poll?.id ?? null,
      familyId: family.familyId,
      utterance: `${credit.name} asked about ${ctx.player}`,
      attribution: credit,
      memory: ctx.memory[elder.id] ?? "",
      decisions: [{
        stage: "elder",
        chosen: elder.id,
        reason: `${elder.name} reports ${family.name}. ${creditReason(credit, ctx.posts.at(-1)?.authorName ?? null)}`,
      }],
    });
    if (post) replies.push({ name: elder.name, familyName: family.name, brief });
  }

  const heard = replies.map((reply) => reply.brief);
  const choice = chooseApproach(heard.length > 0 ? heard : ctx.input.briefs);
  const approachReason = `Big Grumble ${choice.id}: ${choice.reason}.`;
  ctx.bag.traces.push({
    id: nextTraceId(),
    at: ctx.at,
    kind: "elder",
    dinoId: grumble.id,
    dinoName: grumble.name,
    runId: ctx.bag.runId,
    eventId: ctx.input.event,
    eventLabel: ctx.bag.eventLabel,
    input: replies.map((reply) => reply.name).join(", "),
    finalText: approachReason,
    chosenSource: "alignment",
    reason: approachReason,
    candidates: [{ source: "alignment", text: approachReason, ms: 0, note: "approach" }],
    llm: null,
    decisions: [{ stage: "elder", chosen: choice.id, reason: choice.reason }],
    timings: { elder: 0 },
  });
  ctx.pushPost({
    dino: grumble,
    role: "verdict",
    interest: 9,
    reason: `verdict: ${choice.id}`,
    text: verdictLine({ player: ctx.player, replies, choice }),
    link: "child",
    parentId: poll?.id ?? null,
    familyId: null,
    utterance: approachReason,
    attribution: selfCredit,
    memory: "",
    decisions: [{ stage: "elder", chosen: choice.id, reason: choice.reason }],
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
    runId: ctx.bag.runId,
    traces: ctx.bag.traces,
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
  bag: TraceBag;
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
    const brag = ctx.posts.find((post) => post.role === "brag-pay");
    const credit: TraceAttribution = seat.role === "brag-pay" || seat.role === "job-seeking" && !brag
      ? { name: dino.name, dinoId: dino.id, postId: null, because: "self" }
      : brag
        ? { name: brag.authorName, dinoId: brag.authorId, postId: brag.id, because: "lead" }
        : creditSpeaker({
          posts: ctx.posts,
          parentId: link.parentId,
          selfId: dino.id,
          leaderId: dino.id,
          leaderName: dino.name,
        });
    const named = credit.because === "self" ? "" : credit.name;
    const text = seat.role === "brag-pay"
      ? bragLine(dino.id, seat.card)
      : seat.role === "mineral-find"
        ? mineralLine(dino.id, seat.card, named || dino.name)
        : seat.role === "complain-work"
          ? complainLine(dino.id, seat.card, named || dino.name)
          : jobLine(dino.id, named, opening.name);
    const post = ctx.pushPost({
      dino,
      role: seat.role,
      interest: seat.interest,
      reason: `post type: ${seat.role}`,
      text,
      link: link.link,
      parentId: link.parentId,
      familyId: home?.familyId ?? null,
      utterance: `payday ${seat.card.factoryName || opening.name}`,
      attribution: credit,
      memory: ctx.memory[dino.id] ?? "",
      decisions: [
        { stage: "payday", chosen: seat.role, reason: `${dino.name} posts ${seat.role}. Shift ${seat.card.shiftPay}.` },
        { stage: "pick", chosen: credit.name, reason: creditReason(credit, ctx.posts.at(-1)?.authorName ?? null) },
      ],
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
    runId: ctx.bag.runId,
    traces: ctx.bag.traces,
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
    bag: TraceBag;
  },
  leader: RosterDino,
  line: string,
): AllChatLine[] {
  const priorSpeaker = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : null;
  const started = performance.now();
  const accepted = acceptSpeech(line, ctx.prior, priorSpeaker, Math.min(POST_TOKEN_CAP, ctx.chat.maxTokens));
  const ms = roundMs(performance.now() - started);
  if (!accepted) return [];
  const traceId = nextTraceId();
  ctx.bag.traces.push({
    id: traceId,
    at: ctx.at,
    kind: "payday",
    dinoId: leader.id,
    dinoName: leader.name,
    runId: ctx.bag.runId,
    eventId: ctx.input.event,
    eventLabel: ctx.bag.eventLabel,
    input: line,
    finalText: accepted.text,
    chosenSource: "canned",
    reason: "Leader payday line, inside the all-chat budget.",
    candidates: templateCandidates(accepted.text, ms),
    llm: postLlm(leader, "payday", "", feedContext(ctx.bag.eventLabel, "payday", "", ctx.posts)),
    decisions: [{ stage: "payday", chosen: leader.id, reason: "The highest shift speaks the payday line in all-chat." }],
    timings: { template: ms },
  });
  return [{
    id: ctx.id(),
    at: ctx.at,
    runId: ctx.bag.runId,
    eventId: ctx.input.event,
    speakerId: leader.id,
    speakerName: leader.name,
    color: leader.color,
    text: accepted.text,
    tokens: accepted.tokens,
    reason: "leader payday",
    filtered: accepted.filtered,
    traceId,
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
    bag: TraceBag;
  },
  grumble: RosterDino,
  approach: NonNullable<RunResult["approach"]>,
): AllChatLine[] {
  if (ctx.chat.maxLines <= 0 || ctx.chat.maxTokens <= 0) return [];
  const priorSpeaker = ctx.posts.length > 0 ? ctx.posts[ctx.posts.length - 1]!.authorName : null;
  const line = grumbleChatLine(approach, ctx.player);
  const started = performance.now();
  const accepted = acceptSpeech(
    line,
    ctx.prior,
    priorSpeaker,
    Math.min(POST_TOKEN_CAP, ctx.chat.maxTokens),
  );
  const ms = roundMs(performance.now() - started);
  if (!accepted) return [];
  const traceId = nextTraceId();
  ctx.bag.traces.push({
    id: traceId,
    at: ctx.at,
    kind: "elder",
    dinoId: grumble.id,
    dinoName: grumble.name,
    runId: ctx.bag.runId,
    eventId: ctx.input.event,
    eventLabel: ctx.bag.eventLabel,
    input: approach,
    finalText: accepted.text,
    chosenSource: "canned",
    reason: `All-chat follows the elder verdict: ${approach}.`,
    candidates: templateCandidates(accepted.text, ms),
    llm: postLlm(grumble, "verdict", "", feedContext(ctx.bag.eventLabel, approach, ctx.player, ctx.posts)),
    decisions: [{ stage: "elder", chosen: approach, reason: `The canned giant line matches ${approach}.` }],
    timings: { template: ms },
  });
  return [{
    id: ctx.id(),
    at: ctx.at,
    runId: ctx.bag.runId,
    eventId: ctx.input.event,
    speakerId: grumble.id,
    speakerName: grumble.name,
    color: grumble.color,
    text: accepted.text,
    tokens: accepted.tokens,
    reason: `leader demand: ${approach}`,
    filtered: accepted.filtered,
    traceId,
  }];
}

function emptyResult(
  plan: PlanId,
  leaderId: string,
  memory: Record<string, string>,
  nextId: number,
  wages: WageCard[],
  bag: TraceBag,
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
    runId: bag.runId,
    traces: bag.traces,
  };
}

export function postTokensOk(posts: readonly { content: string }[], cap = POST_TOKEN_CAP): boolean {
  return posts.every((post) => estimateTokens(post.content) <= cap);
}
