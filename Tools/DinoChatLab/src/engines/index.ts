import type { EngineId } from "../logic/types";
import { createAimlEngine } from "./aimlEngine";
import { createElizaEngine } from "./elizaEngine";
import { createMegahalEngine, type MegaHalRng } from "./megahalEngine";
import { createRiveScriptEngine } from "./rivescriptEngine";
import type { ChatEngine, WebLlmClient } from "./types";
import { createWebLlmEngine } from "./webllmEngine";

export function createEngines(getWebLlm: () => WebLlmClient | null, rng?: MegaHalRng): Record<EngineId, ChatEngine> {
  return {
    eliza: createElizaEngine(),
    aiml: createAimlEngine(),
    rivescript: createRiveScriptEngine(),
    megahal: createMegahalEngine(rng),
    webllm: createWebLlmEngine(getWebLlm),
  };
}
