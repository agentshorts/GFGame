import { BEG_MANGO, EXTORT_LINE } from "../logic/alignment";
import { fillTarget } from "../logic/fillTarget";
import type { Alignment } from "../logic/types";
import { BIG_GRUMBLE_ID, eldersOf, familyOf, type FamilyTree } from "./tree";
import type {
  DinobookEventId,
  FamilyBrief,
  GrumbleApproach,
  PlanId,
  RoleId,
  RosterDino,
} from "./types";

const HARSH: readonly Alignment[] = ["LE", "NE", "CE", "CN"];

const VOICE: Record<string, string> = {
  "lanternmaw": "Lure light.",
  tusksaw: "Burrowed.",
  ripwing: "Dive.",
  scythepaw: "Claws folded.",
  "chatty-bird": "Hi hi!",
  [BIG_GRUMBLE_ID]: "Huh.",
};

const BLOCK_PREF = ["tusksaw", "lanternmaw", "ripwing", "scythepaw", "chatty-bird"];
const HIT_PREF = ["scythepaw", "tusksaw", "ripwing", "lanternmaw", "chatty-bird"];
const SCREEN_PREF = ["ripwing", "tusksaw", "lanternmaw", "scythepaw"];

const ROLE_ORDER: Record<RoleId, number> = {
  lead: 0,
  "block-path": 1,
  "take-hit": 2,
  screen: 3,
  warn: 4,
  greet: 5,
  report: 6,
  verdict: 7,
  "brag-pay": 1,
  "mineral-find": 2,
  "complain-work": 3,
  "job-seeking": 4,
};

export interface Assignment {
  dinoId: string;
  role: RoleId;
  interest: number;
}

export function voiceTag(dinoId: string): string {
  return VOICE[dinoId] ?? "Hmm.";
}

export function isHarsh(alignment: Alignment): boolean {
  return HARSH.includes(alignment);
}

export function briefFor(briefs: readonly FamilyBrief[], familyId: string | null): FamilyBrief | null {
  if (!familyId) return null;
  return briefs.find((brief) => brief.familyId === familyId) ?? null;
}

function byId(roster: readonly RosterDino[], id: string): RosterDino | null {
  return roster.find((dino) => dino.id === id) ?? null;
}

function harshScore(alignment: Alignment): number {
  const order = ["LE", "NE", "CE", "CN", "N", "LN", "NG", "CG", "LG"];
  const index = order.indexOf(alignment);
  return index < 0 ? 99 : index;
}

function kindScore(alignment: Alignment): number {
  const order = ["NG", "LG", "CG", "LN", "N", "CN", "NE", "LE", "CE"];
  const index = order.indexOf(alignment);
  return index < 0 ? 99 : index;
}

export function pickLeader(
  event: DinobookEventId,
  tree: FamilyTree,
  roster: readonly RosterDino[],
  briefs: readonly FamilyBrief[],
): string {
  const elders = eldersOf(tree)
    .map((family) => byId(roster, family.elderId))
    .filter((dino): dino is RosterDino => !!dino);
  if (elders.length === 0) return roster[0]?.id ?? BIG_GRUMBLE_ID;

  const repOf = (dino: RosterDino) => briefFor(briefs, familyOf(tree, dino.id)?.familyId ?? null)?.rep ?? 0;
  const mangoOf = (dino: RosterDino) => briefFor(briefs, familyOf(tree, dino.id)?.familyId ?? null)?.mangoRefused ?? 0;
  const rentOf = (dino: RosterDino) => briefFor(briefs, familyOf(tree, dino.id)?.familyId ?? null)?.rentalsDowned ?? 0;

  const sorted = [...elders];
  if (event === "player-joined") {
    sorted.sort((a, b) => repOf(b) - repOf(a) || kindScore(a.alignment) - kindScore(b.alignment) || a.id.localeCompare(b.id));
  } else if (event === "refused-mango") {
    sorted.sort((a, b) => mangoOf(b) - mangoOf(a) || harshScore(a.alignment) - harshScore(b.alignment) || a.id.localeCompare(b.id));
  } else if (event === "rental-downed") {
    sorted.sort((a, b) => rentOf(b) - rentOf(a) || repOf(a) - repOf(b) || a.id.localeCompare(b.id));
  } else {
    sorted.sort((a, b) => repOf(a) - repOf(b) || harshScore(a.alignment) - harshScore(b.alignment) || a.id.localeCompare(b.id));
  }
  return sorted[0]!.id;
}

export function planFor(event: DinobookEventId, leader: RosterDino | null): PlanId {
  if (event === "summon-big-grumble") return "poll";
  if (event === "payday") return "payday";
  if (event === "player-joined") return "welcome";
  if (event === "rental-downed") return leader && isHarsh(leader.alignment) ? "repossess" : "cover";
  if (leader && !isHarsh(leader.alignment)) return "cover";
  return "toll";
}

function takeFree(pref: readonly string[], roster: readonly RosterDino[], used: Set<string>): string | null {
  for (const id of pref) {
    if (used.has(id)) continue;
    if (!roster.some((dino) => dino.id === id)) continue;
    return id;
  }
  return null;
}

export function assignRoles(
  plan: PlanId,
  leaderId: string,
  tree: FamilyTree,
  roster: readonly RosterDino[],
  briefs: readonly FamilyBrief[],
): Assignment[] {
  if (plan === "poll") return [];
  const used = new Set<string>([leaderId]);
  const assigned: Assignment[] = [{ dinoId: leaderId, role: "lead", interest: 9 }];

  if (plan === "welcome") {
    for (const family of eldersOf(tree)) {
      if (used.has(family.elderId)) continue;
      if (!roster.some((dino) => dino.id === family.elderId)) continue;
      const band = briefFor(briefs, family.familyId)?.band;
      if (band === "hostile" || band === "wary") {
        used.add(family.elderId);
        assigned.push({ dinoId: family.elderId, role: "warn", interest: 7 });
      }
    }
    const leaderFriends = tree.sideLinks.flatMap((link) => {
      if (link.a === leaderId) return [link.b];
      if (link.b === leaderId) return [link.a];
      return [];
    });
    for (const friendId of leaderFriends) {
      if (used.has(friendId)) continue;
      if (!roster.some((dino) => dino.id === friendId)) continue;
      used.add(friendId);
      assigned.push({ dinoId: friendId, role: "greet", interest: 6 });
    }
    return assigned.sort((a, b) => ROLE_ORDER[a.role] - ROLE_ORDER[b.role] || a.dinoId.localeCompare(b.dinoId));
  }

  const block = takeFree(BLOCK_PREF, roster, used);
  if (block) {
    used.add(block);
    assigned.push({ dinoId: block, role: "block-path", interest: 8 });
  }
  const hit = takeFree(HIT_PREF, roster, used);
  if (hit) {
    used.add(hit);
    assigned.push({ dinoId: hit, role: "take-hit", interest: 8 });
  }
  const screen = takeFree(SCREEN_PREF, roster, used);
  if (screen) {
    used.add(screen);
    assigned.push({ dinoId: screen, role: "screen", interest: 7 });
  }

  if (roster.some((dino) => dino.id === "chatty-bird") && !used.has("chatty-bird")) {
    const friendIn = assigned.some((row) => {
      return tree.sideLinks.some((link) => (
        (link.a === "chatty-bird" && link.b === row.dinoId) ||
        (link.b === "chatty-bird" && link.a === row.dinoId)
      ));
    });
    if (friendIn) assigned.push({ dinoId: "chatty-bird", role: "greet", interest: 5 });
  }

  return assigned.sort((a, b) => ROLE_ORDER[a.role] - ROLE_ORDER[b.role] || a.dinoId.localeCompare(b.dinoId));
}

function eventPhrase(event: DinobookEventId): string {
  if (event === "attacked") return "attacked us";
  if (event === "refused-mango") return "refused mango";
  if (event === "rental-downed") return "downed a rental";
  if (event === "player-joined") return "joined";
  return "is here";
}

function nameOf(roster: readonly RosterDino[], id: string | undefined, fallback: string): string {
  if (!id) return fallback;
  return roster.find((dino) => dino.id === id)?.name ?? fallback;
}

export function leadLine(input: {
  leader: RosterDino;
  plan: PlanId;
  event: DinobookEventId;
  player: string;
  familyName: string;
  roster: readonly RosterDino[];
  assignments: readonly Assignment[];
  memory: string;
}): string {
  const tag = voiceTag(input.leader.id);
  const blockId = input.assignments.find((row) => row.role === "block-path")?.dinoId;
  const hitId = input.assignments.find((row) => row.role === "take-hit")?.dinoId;
  const blockName = nameOf(input.roster, blockId, "someone");
  const hitName = hitId ? nameOf(input.roster, hitId, "someone") : input.leader.name;
  const hitClause = hitId ? `${hitName} takes the hit` : "I take the hit";
  const noted = input.memory ? ` Still noted: ${input.memory}` : "";
  const what = eventPhrase(input.event);

  if (input.plan === "welcome") {
    return `${tag} ${input.player} joined. ${input.familyName} welcomes them. Friends can wave on the side. No path block.${noted}`;
  }
  if (input.plan === "cover") {
    return `${tag} ${input.player} ${what}. ${input.familyName} plan: ${blockName} blocks the path, ${hitClause}, then we ask for mango. Read it here first.${noted}`;
  }
  if (input.plan === "repossess") {
    return `${tag} ${input.player} downed a rental. ${input.familyName} plan: ${blockName} blocks the path, ${hitClause}, then I demand it back.${noted}`;
  }
  return `${tag} ${input.player} ${what}. ${input.familyName} plan: ${blockName} blocks the path, ${hitClause}, then I demand mango. Read Dinobook before all-chat.${noted}`;
}

export function roleLine(input: {
  dino: RosterDino;
  role: RoleId;
  priorName: string;
  player: string;
  familyName: string;
}): string {
  const tag = voiceTag(input.dino.id);
  const heard = `${input.priorName} said it, so`;
  if (input.role === "block-path") {
    const extra = input.dino.id === "tusksaw" ? " Spines rattling." : "";
    return `${tag} ${heard} I block ${input.player}'s path.${extra}`;
  }
  if (input.role === "take-hit") {
    return `${tag} ${heard} I take the hit on purpose. ${input.player} reaches me first.`;
  }
  if (input.role === "screen") {
    const extra = input.dino.id === "ripwing" ? " Screech." : "";
    return `${tag} ${heard} I screen the side.${extra}`;
  }
  if (input.role === "warn") {
    return `${tag} ${heard} ${input.familyName} is not waving. I still count ${input.player}'s record.`;
  }
  return `${tag} ${heard} I wave from the side. I'm still listening.`;
}

export function demandLine(leader: RosterDino, plan: PlanId, player: string): string {
  if (plan === "welcome") return fillTarget("Hi hi, {Target}! I'm here!", player === "you" ? "" : player);
  if (plan === "repossess") return `${player}, the rental is down. Bring it back.`;
  if (plan === "cover") return BEG_MANGO;
  return EXTORT_LINE;
}

export interface ApproachChoice {
  id: GrumbleApproach;
  reason: string;
}

/** Big Grumble's approach is the elders' replies, in this order. */
export function chooseApproach(briefs: readonly FamilyBrief[]): ApproachChoice {
  const elderKills = briefs.reduce((sum, brief) => sum + brief.elderKills, 0);
  const kills = briefs.reduce((sum, brief) => sum + brief.kills, 0);
  const hostile = briefs.filter((brief) => brief.band === "hostile").length;
  const friendly = briefs.filter((brief) => brief.band === "friendly").length;
  const bounties = briefs.reduce((sum, brief) => sum + brief.bountiesOnPlayer + brief.bountiesClaimed, 0);
  const rentals = briefs.reduce((sum, brief) => sum + brief.rentalsDowned, 0);
  const mangoRefused = briefs.reduce((sum, brief) => sum + brief.mangoRefused, 0);
  const mangoGiven = briefs.reduce((sum, brief) => sum + brief.mangoGiven, 0);

  if (elderKills > 0) return { id: "chase", reason: "an elder was killed" };
  if (hostile > 0 && kills > 0) return { id: "chase", reason: "a hostile family has kills" };
  if (bounties > 0) return { id: "collect-bounty", reason: "bounties are open" };
  if (rentals > 0) return { id: "return-rentals", reason: "a rental is down" };
  if (mangoRefused > mangoGiven) return { id: "demand-mango", reason: "mango was refused more than it was given" };
  if (kills === 0 && hostile === 0 && friendly > 0) {
    return { id: "play", reason: "the elders are friendly and the kill count is clear" };
  }
  return { id: "watch", reason: "the elders do not agree" };
}

export function grumbleChatLine(approach: GrumbleApproach, player: string): string {
  if (approach === "chase") return "Why you running, huh? Why you running?";
  if (approach === "play") return "Hey! Wait up! I just want to play!";
  if (approach === "collect-bounty") return "Found you! Your hiding is very bad.";
  if (approach === "return-rentals") return "Nice house. Be a shame if I... looked at it.";
  if (approach === "demand-mango") return fillTarget("Don't drop your rocks, {Target}! Actually, do.", player === "you" ? "" : player);
  return "I'm not chasing, I'm escorting. Loudly.";
}

export function elderReport(input: {
  elder: RosterDino;
  familyName: string;
  brief: FamilyBrief;
  priorName: string;
  player: string;
}): string {
  const brief = input.brief;
  return [
    `${voiceTag(input.elder.id)} ${input.priorName} asked.`,
    `${input.familyName} rep ${brief.rep} (${brief.band}).`,
    `kills ${brief.kills}, elder kills ${brief.elderKills}.`,
    `bounties ${brief.bountiesOnPlayer} on the player, ${brief.bountiesClaimed} claimed.`,
    `rentals ${brief.rentalsDowned} downed, ${brief.rentalsLate} late, ${brief.rentalsReturned} returned.`,
  ].join(" ");
}

export function verdictLine(input: {
  player: string;
  replies: readonly { name: string; familyName: string; brief: FamilyBrief }[];
  choice: ApproachChoice;
}): string {
  const heard = input.replies.map((reply) => (
    `${reply.name} says ${reply.familyName} rep ${reply.brief.rep}`
  )).join(". ");
  const prefix = heard.length > 0 ? `${heard}. ` : "";
  return `Huh. ${prefix}${input.choice.reason}, so I ${input.choice.id === "play" ? "just want to play" : input.choice.id} around ${input.player}.`;
}

export function followUpChat(speaker: RosterDino, priorName: string, player: string): string {
  return `${voiceTag(speaker.id)} ${priorName} already spoke. I stay on ${player} and add nothing new.`;
}
