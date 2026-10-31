import type { EngineId } from "../logic/types";
import { createAimlEngine } from "./aimlEngine";
import { createCreatureEngine } from "./creatureEliza";
import { createElizaEngine } from "./elizaEngine";
import { createMegahalEngine, type MegaHalRng } from "./megahalEngine";
import { createRiveScriptEngine } from "./rivescriptEngine";
import type { ChatEngine } from "./types";

export function createEngines(rng?: MegaHalRng): Record<EngineId, ChatEngine> {
  return {
    creature: createCreatureEngine(),
    eliza: createElizaEngine(),
    aiml: createAimlEngine(),
    rivescript: createRiveScriptEngine(),
    megahal: createMegahalEngine(rng),
  };
}
