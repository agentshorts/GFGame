import { MegaHal } from "megahal";
import { trainingLines } from "../logic/persona";
import type { Persona } from "../logic/types";
import type { ChatEngine, EngineReply, EngineRequest } from "./types";

export interface MegaHalRng {
  randomRange(min: number, max: number): number;
}

export function createMegahalEngine(rng?: MegaHalRng): ChatEngine {
  const bots = new Map<string, MegaHal>();

  function ready(persona: Persona): Promise<void> {
    if (bots.has(persona.id)) return Promise.resolve();
    const hal = new MegaHal(3, rng ?? null);
    hal.setLimit({ timeout: 400, maxIterations: 40 });
    const fallback = persona.fallbackReplies[0] ?? "...";
    hal.setFallbackReply(fallback);
    for (const line of trainingLines(persona)) {
      hal.learn(line);
    }
    bots.set(persona.id, hal);
    return Promise.resolve();
  }

  return {
    id: "megahal",
    label: "MegaHAL",
    blurb: "Markov chatter trained on this dino's lines.",
    needsModel: false,
    ready,
    async reply(request: EngineRequest): Promise<EngineReply> {
      await ready(request.persona);
      const hal = bots.get(request.persona.id);
      if (!hal) throw new Error(`MegaHAL was not ready for ${request.persona.name}.`);
      const generated = hal.generate(request.utterance);
      return {
        engine: "megahal",
        text: (generated && generated.trim().length > 0)
          ? generated.trim()
          : (request.persona.fallbackReplies[0] ?? "..."),
      };
    },
    reset(personaId?: string) {
      if (personaId) bots.delete(personaId);
      else bots.clear();
    },
  };
}
