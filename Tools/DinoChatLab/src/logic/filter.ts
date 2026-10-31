import {
  RegExpMatcher,
  TextCensor,
  asteriskCensorStrategy,
  englishDataset,
  englishRecommendedTransformers,
} from "obscenity";
import type { TraceDecision } from "./traceTypes";
import type { Replacement } from "./types";

const matcher = new RegExpMatcher({
  ...englishDataset.build(),
  ...englishRecommendedTransformers,
});

const censor = new TextCensor().setStrategy(asteriskCensorStrategy());

export interface FilterResult {
  text: string;
  filtered: boolean;
  replacements: Replacement[];
}

export function filterOutput(text: string): FilterResult {
  const matches = matcher.getAllMatches(text, true);
  if (matches.length === 0) {
    return { text, filtered: false, replacements: [] };
  }
  const cleaned = censor.applyTo(text, matches);
  const replacements = matches.map((match) => {
    const original = text.slice(match.startIndex, match.endIndex);
    return { original, replacement: "*".repeat(Math.max(1, original.length)) };
  });
  return { text: cleaned, filtered: true, replacements };
}

export function filterDecision(result: FilterResult): TraceDecision {
  if (!result.filtered) {
    return {
      stage: "filter",
      chosen: result.text,
      reason: "PG filter left the line alone.",
      detail: { filtered: false, spans: 0 },
    };
  }
  const words = result.replacements.map((swap) => swap.original).filter((word) => word.length > 0);
  const count = result.replacements.length;
  return {
    stage: "filter",
    chosen: result.text,
    reason: words.length > 0
      ? `PG filter hid ${count} span${count === 1 ? "" : "s"} (${words.join(", ")}).`
      : `PG filter hid ${count} span${count === 1 ? "" : "s"}.`,
    detail: { filtered: true, spans: count },
  };
}
