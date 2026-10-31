<script setup lang="ts">
import { SOURCE_LABEL } from "../logic/trace";
import type { TraceEvent } from "../logic/traceTypes";

defineProps<{ trace: TraceEvent | null }>();

function timingBits(trace: TraceEvent): string {
  const parts = Object.entries(trace.timings)
    .filter(([, ms]) => ms > 0)
    .map(([name, ms]) => `${SOURCE_LABEL[name as keyof typeof SOURCE_LABEL] ?? name} ${ms} ms`);
  return parts.join(" · ");
}
</script>

<template>
  <details v-if="trace" class="trace">
    <summary>
      <span class="badge" :data-source="trace.chosenSource">{{ SOURCE_LABEL[trace.chosenSource] }}</span>
      Trace
      <template v-if="trace.timings.total"> · {{ trace.timings.total }} ms</template>
    </summary>
    <p class="trace-reason">{{ trace.reason }}</p>
    <p v-if="trace.attribution && trace.attribution.because !== 'self'" class="trace-reason">
      Credit: {{ trace.attribution.name }}
      ({{ trace.attribution.because === "parent" ? "the post this hangs under" : "the line that opened the plan" }})
    </p>
    <div class="trace-grid">
      <article
        v-for="candidate in trace.candidates"
        :key="candidate.source"
        class="cand"
        :class="{ chosen: candidate.source === trace.chosenSource }"
      >
        <p class="cand-top">
          <span class="badge" :data-source="candidate.source">{{ SOURCE_LABEL[candidate.source] }}</span>
          <span v-if="candidate.score != null" class="cand-ms">overlap {{ candidate.score }}</span>
          <span class="cand-ms">{{ candidate.ms }} ms</span>
        </p>
        <p class="cand-text" :class="{ none: !candidate.text }">{{ candidate.text ?? "none" }}</p>
        <p v-if="candidate.note" class="cand-note">{{ candidate.note }}</p>
      </article>
    </div>
    <p v-if="timingBits(trace)" class="cand-note">Timings: {{ timingBits(trace) }}</p>
    <ul class="trace-steps">
      <li v-for="(step, index) in trace.decisions" :key="index">
        <span class="badge step">{{ step.stage }}</span>
        {{ step.reason }}
      </li>
    </ul>
    <details v-if="trace.llm" class="trace-llm">
      <summary>Prompt and context{{ trace.llm.loaded ? "" : " · model not loaded" }}</summary>
      <p class="cand-note">Persona</p>
      <pre>{{ trace.llm.persona || "(empty)" }}</pre>
      <p class="cand-note">Memory</p>
      <pre>{{ trace.llm.memory || "(empty)" }}</pre>
      <p class="cand-note">Context layer</p>
      <pre>{{ trace.llm.context || "(empty)" }}</pre>
      <p class="cand-note">Prompt</p>
      <pre>{{ trace.llm.prompt || "(empty)" }}</pre>
      <p v-if="trace.llm.steerText" class="cand-note">Steer reply</p>
      <pre v-if="trace.llm.steerText">{{ trace.llm.steerText }}</pre>
    </details>
  </details>
</template>

<style scoped>
.trace {
  margin: 0.35rem 0 0.15rem;
  font-size: 0.78rem;
  color: var(--paper-ink, #2a2118);
}
.trace summary {
  cursor: pointer;
  color: var(--paper-muted, #6d5c4a);
}
.trace-reason { margin: 0.35rem 0; }
.trace-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(9.5rem, 1fr));
  gap: 0.35rem;
  margin: 0.35rem 0;
}
.cand {
  border: 1px solid rgba(90, 62, 36, 0.2);
  border-radius: 8px;
  padding: 0.35rem 0.45rem;
  background: #fffaf2;
  min-width: 0;
}
.cand.chosen {
  border-color: #8a5a12;
  box-shadow: inset 0 0 0 2px #e0a15a;
  background: #fff3df;
}
.cand-top {
  display: flex;
  flex-wrap: wrap;
  gap: 0.25rem 0.4rem;
  align-items: center;
  margin: 0 0 0.25rem;
}
.cand-text { margin: 0; white-space: pre-wrap; }
.cand-text.none { color: var(--paper-muted, #6d5c4a); font-style: italic; }
.cand-note, .cand-ms { color: var(--paper-muted, #6d5c4a); font-size: 0.72rem; }
.cand-ms { margin-left: auto; }
.badge {
  display: inline-block;
  border-radius: 99px;
  padding: 0.05rem 0.4rem;
  font-size: 0.68rem;
  font-weight: 700;
  letter-spacing: 0.02em;
  color: #2a2118;
  background: #e6d7c3;
}
.badge[data-source="eliza"] { background: #f0c27a; }
.badge[data-source="aiml"] { background: #9fd6c8; }
.badge[data-source="rivescript"] { background: #d7c4f3; }
.badge[data-source="megahal"] { background: #b7d39a; }
.badge[data-source="webllm"] { background: #9ec5ea; }
.badge[data-source="canned"] { background: #e7b89a; }
.badge[data-source="fallback"] { background: #e7a090; }
.badge[data-source="alignment"] { background: #f3e29a; }
.badge[data-source="stats"] { background: #d9d0c4; }
.badge[data-source="none"] { background: #e4e0da; }
.badge.step { background: #efe4d2; }
.trace-steps { margin: 0.2rem 0 0.4rem; padding-left: 0; list-style: none; }
.trace-steps li { margin: 0.2rem 0; }
.trace-llm pre {
  white-space: pre-wrap;
  max-height: 8rem;
  overflow: auto;
  background: #fffaf2;
  border-radius: 6px;
  padding: 0.35rem 0.45rem;
  margin: 0.15rem 0 0.4rem;
}
</style>
