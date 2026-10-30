import {
  RegExpMatcher,
  TextCensor,
  asteriskCensorStrategy,
  englishDataset,
  englishRecommendedTransformers,
} from "obscenity";
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
