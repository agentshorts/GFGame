import {
  decideInterest,
  REP_BAND_THRESHOLDS,
  repBand,
  scoreInterest as coreScore,
  type RelationshipKind,
} from "../core";
import type { DinoProfile, InterestMins } from "./profile";

export interface InterestScore {
  interest: number;
  action: "ignore" | "like" | "reply";
  hits: string[];
  reason: string;
  band: ReturnType<typeof repBand>;
}

function authoredKind(profile: DinoProfile, playerName: string): { kind: RelationshipKind; affinity: number; hits: string[] } {
  const rel = profile.relationships.find((row) => row.subjectKey.toLowerCase() === playerName.trim().toLowerCase());
  if (!rel) return { kind: "neutral", affinity: 0, hits: [] };
  return { kind: rel.kind, affinity: rel.affinity, hits: [`${rel.kind} ${rel.subjectKey}`] };
}

/** Uses the memory core's score and the frozen rep bands. The comment minimum is `interestMin`. */
export function scoreInterest(message: string, profile: DinoProfile, playerName: string, rep = 0): InterestScore {
  const authored = authoredKind(profile, playerName);
  const band = repBand(rep, REP_BAND_THRESHOLDS);
  const interest = coreScore({
    text: message,
    topics: profile.topics,
    relationshipKind: authored.kind,
    affinity: authored.affinity,
    repBand: band,
  });
  const decision = decideInterest({
    interest,
    thresholds: {
      interestMin: profile.interest.comment,
      replyToReplyInterest: profile.interest.engagement,
      postInterestMin: profile.interest.post,
    },
    relationshipKind: authored.kind,
    speakerIsDino: false,
    speakerIsSelf: false,
  });
  const hits = profile.topics
    .filter((topic) => message.toLowerCase().includes(topic.name.toLowerCase()))
    .map((topic) => `${topic.name} (${topic.sentiment})`);
  const action = decision.action === "reply" || decision.action === "like" ? decision.action : "ignore";
  return { interest, action, hits: [...hits, ...authored.hits], reason: decision.reason, band };
}

export function gateAction(interest: number, mins: InterestMins): "ignore" | "like" | "reply" {
  const decision = decideInterest({
    interest,
    thresholds: {
      interestMin: mins.comment,
      replyToReplyInterest: mins.engagement,
      postInterestMin: mins.post,
    },
    relationshipKind: "neutral",
    speakerIsDino: false,
    speakerIsSelf: false,
  });
  if (decision.action === "reply" || decision.action === "like") return decision.action;
  return "ignore";
}
