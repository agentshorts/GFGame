import type { DinoProfile, InterestMins } from "./profile";
import { topicKeyword } from "./profile";
import { DEFAULT_QUOTA, type SpeechQuota } from "./shim/untilCore";

export interface InterestScore {
  interest: number;
  action: "ignore" | "like" | "reply";
  hits: string[];
}

function mentions(message: string, name: string): boolean {
  const hay = message.toLowerCase();
  if (hay.includes(name.toLowerCase())) return true;
  const keyword = topicKeyword(name);
  if (!keyword) return false;
  return new RegExp(`(?:^|[^a-z0-9])${keyword}(?:[^a-z0-9]|$)`).test(hay);
}

/**
 * Lab stand-in for JacobApp's interest JSON until a model is loaded.
 * A plain line starts at 5, so the default comment minimum of 4 still replies.
 */
export function scoreInterest(message: string, profile: DinoProfile, playerName: string): InterestScore {
  let interest = 5;
  const hits: string[] = [];
  for (const topic of profile.topics) {
    if (!mentions(message, topic.name)) continue;
    hits.push(`${topic.name} (${topic.sentiment})`);
    if (topic.sentiment === "like") interest += 3;
    else if (topic.sentiment === "dislike") interest += 2;
    else interest += 1;
  }
  const rel = profile.relationships.find((row) => row.subjectKey.toLowerCase() === playerName.trim().toLowerCase());
  if (rel && (rel.kind === "friend" || rel.kind === "rival")) {
    interest += 2;
    hits.push(`${rel.kind} ${rel.subjectKey}`);
  } else if (rel?.kind === "family") {
    interest += 1;
    hits.push(`family ${rel.subjectKey}`);
  }
  interest = Math.max(1, Math.min(10, interest));
  return { interest, action: gateAction(interest, profile.interest), hits };
}

export function gateAction(interest: number, mins: InterestMins): "ignore" | "like" | "reply" {
  if (interest < mins.comment) return "ignore";
  if (interest === mins.comment) return "like";
  return "reply";
}

export function quotaVerdict(quota: SpeechQuota = DEFAULT_QUOTA, roll = 0): { ok: boolean; reason: string } {
  if (quota.speakChance <= 0) return { ok: false, reason: "speak chance is zero" };
  if (roll > quota.speakChance) return { ok: false, reason: "speak chance roll missed" };
  return { ok: true, reason: "default quota allows this test" };
}
