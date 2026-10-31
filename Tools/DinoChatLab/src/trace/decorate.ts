import { createEngines } from "../engines";
import type { Persona } from "../logic/types";
import { mergeCandidates, probeClassic, traceEnabled } from "../logic/trace";
import type { TraceEvent } from "../logic/traceTypes";

let engines: ReturnType<typeof createEngines> | null = null;

function classicEngines() {
  engines ??= createEngines(() => null);
  return engines;
}

/** Fill ELIZA, AIML, RiveScript, and MegaHAL beside a template line. One call each. */
export async function decorateDinobook(traces: readonly TraceEvent[], personas: readonly Persona[]): Promise<TraceEvent[]> {
  if (!traceEnabled()) return traces.map((trace) => ({ ...trace }));
  const pack = classicEngines();
  const next: TraceEvent[] = [];
  for (const trace of traces) {
    const persona = personas.find((row) => row.id === trace.dinoId);
    const utterance = trace.input?.trim() ?? "";
    if (!persona || utterance.length === 0 || trace.chosenSource === "alignment") {
      next.push(trace);
      continue;
    }
    const probed = await probeClassic((source) => pack[source].reply({
      persona,
      history: [],
      utterance,
      mode: "event",
      systemPrompt: trace.llm?.persona ?? persona.name,
      temperature: 0.7,
      maxTokens: 80,
      peerNames: [],
    }).then((reply) => ({ text: reply.text, fellBack: reply.fellBack })));
    const timings = { ...trace.timings };
    for (const candidate of probed) timings[candidate.source] = candidate.ms;
    next.push({
      ...trace,
      candidates: mergeCandidates(trace.candidates, probed),
      timings,
    });
  }
  return next;
}
