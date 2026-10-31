import { getAssembledReply, loadElizaInEnglish, type Eliza } from "eliza-core";
import { from } from "rxjs";
import { elizaSource } from "../logic/scripts";
import type { Persona } from "../logic/types";
import type { ChatEngine, EngineReply, EngineRequest } from "./types";

export function createElizaEngine(): ChatEngine {
  const bots = new Map<string, Eliza>();

  async function ready(persona: Persona): Promise<void> {
    if (bots.has(persona.id)) return;
    const script = `${elizaSource(persona)}\n`;
    const bot = await loadElizaInEnglish(from([script]));
    bots.set(persona.id, bot);
  }

  return {
    id: "eliza",
    label: "ELIZA",
    blurb: "Keyword script. Works with no download.",
    needsModel: false,
    ready,
    async reply(request: EngineRequest): Promise<EngineReply> {
      await ready(request.persona);
      const bot = bots.get(request.persona.id);
      if (!bot) throw new Error(`ELIZA was not ready for ${request.persona.name}.`);
      const assembled = getAssembledReply(bot.processInput(request.utterance))?.trim();
      if (assembled && assembled.length > 0) return { engine: "eliza", text: assembled };
      return { engine: "eliza", text: request.persona.fallbackReplies[0] ?? "...", fellBack: true };
    },
    reset(personaId?: string) {
      if (personaId) bots.delete(personaId);
      else bots.clear();
    },
  };
}
