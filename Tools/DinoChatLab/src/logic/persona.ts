import { z } from "zod";
import { ALIGNMENTS, type Persona } from "./types";

const keyword = z.string().regex(/^[a-z][a-z0-9]*$/, "keyword must be one lowercase word");

export const personaSchema = z.object({
  id: z.string().regex(/^[a-z0-9-]+$/),
  name: z.string().min(1),
  color: z.string().regex(/^#[0-9a-fA-F]{6}$/),
  alignment: z.enum(ALIGNMENTS),
  wants: z.array(z.object({
    id: z.string().regex(/^[a-z0-9-]+$/),
    name: z.string().min(1),
  })).min(1),
  personality: z.string().min(1),
  speechStyle: z.string().min(1),
  pgRule: z.string().min(1),
  maxReplyLength: z.int().min(24).max(200),
  exampleLinesCanon: z.boolean(),
  exampleLines: z.array(z.string().min(1)).min(1),
  loreFromRepo: z.array(z.string()),
  loreTodo: z.array(z.string().min(1)).min(1),
  greeting: z.string().min(1),
  farewell: z.string().min(1),
  fallbackReplies: z.array(z.string().min(1)).min(1),
  cues: z.array(z.object({
    keywords: z.array(keyword).min(1),
    replies: z.array(z.string().min(1)).min(1),
  })).min(1),
  rawScripts: z.object({
    eliza: z.string().optional(),
    aiml: z.string().optional(),
    rivescript: z.string().optional(),
  }).optional(),
});

export function parsePersona(data: unknown): Persona {
  const persona = personaSchema.parse(data);
  const keywords = persona.cues.flatMap((cue) => cue.keywords);
  const duplicate = keywords.find((word, index) => keywords.indexOf(word) !== index);
  if (duplicate) {
    throw new Error(`Duplicate keyword "${duplicate}" on ${persona.id}`);
  }
  return persona;
}

export function trainingLines(persona: Persona): string[] {
  const lines = [
    persona.greeting,
    persona.farewell,
    ...persona.exampleLines,
    ...persona.fallbackReplies,
    ...persona.cues.flatMap((cue) => cue.replies),
  ];
  return [...new Set(lines)];
}
