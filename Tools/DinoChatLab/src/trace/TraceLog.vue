<script setup lang="ts">
import { computed, ref } from "vue";
import { clearTraces, filterTraces, SOURCE_LABEL, tracesToJson } from "../logic/trace";
import type { TraceSource } from "../logic/traceTypes";
import TraceRow from "./TraceRow.vue";
import TraceToggle from "./TraceToggle.vue";
import { dayKey, formatDay, formatWhen } from "./time";
import { useTrace } from "./useTrace";

const { enabled, events } = useTrace();
const dinoId = ref("all");
const source = ref("all");

const dinos = computed(() => {
  const seen = new Map<string, string>();
  for (const event of events.value) {
    if (!seen.has(event.dinoId)) seen.set(event.dinoId, event.dinoName);
  }
  return [...seen.entries()].map(([id, name]) => ({ id, name }));
});

const sources: { id: TraceSource; label: string }[] = (
  ["eliza", "aiml", "rivescript", "megahal", "webllm", "canned", "fallback", "stats", "alignment", "none"] as const
).map((id) => ({ id, label: SOURCE_LABEL[id] }));

const shown = computed(() => filterTraces(events.value, { dinoId: dinoId.value, source: source.value }));

function showDay(index: number): boolean {
  const event = shown.value[index];
  if (!event) return false;
  if (index === 0) return true;
  const prev = shown.value[index - 1];
  return !prev || dayKey(event.at) !== dayKey(prev.at);
}

function download() {
  const blob = new Blob([tracesToJson(shown.value, new Date().toISOString())], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "dino-chat-traces.json";
  link.click();
  URL.revokeObjectURL(url);
}

function wipe() {
  clearTraces();
}
</script>

<template>
  <div class="lab">
    <header class="top">
      <div>
        <p class="eyebrow">Trace log</p>
        <h1>What each engine said</h1>
        <a class="lab-link" href="#/">Back to the chat lab</a>
        <a class="lab-link" href="#/personas">Personas</a>
      </div>
      <div class="status trace-status">
        <TraceToggle />
        <p>{{ events.length }} events · showing {{ shown.length }}</p>
      </div>
    </header>
    <section class="log trace-page" aria-label="Trace log">
      <div class="log-head">
        <div class="row">
          <label class="field">
            <span>Dino</span>
            <select v-model="dinoId">
              <option value="all">All dinos</option>
              <option v-for="dino in dinos" :key="dino.id" :value="dino.id">{{ dino.name }}</option>
            </select>
          </label>
          <label class="field">
            <span>Source</span>
            <select v-model="source">
              <option value="all">All sources</option>
              <option v-for="row in sources" :key="row.id" :value="row.id">{{ row.label }}</option>
            </select>
          </label>
        </div>
        <div class="row">
          <button type="button" @click="download">Export JSON</button>
          <button type="button" @click="wipe">Clear log</button>
        </div>
        <p class="hint" v-if="!enabled">Trace is off. New lines keep the chosen engine only. Older probes stay in this list.</p>
        <p class="hint" v-else>Chat turns, Dinobook posts, paydays, and elder picks, oldest first.</p>
      </div>
      <div class="sheet">
        <p class="hint" v-if="shown.length === 0">No traces yet. Say something in the lab, or press a Dinobook event.</p>
        <article v-for="(event, index) in shown" :key="event.id" class="bubble">
          <p v-if="showDay(index)" class="day-rule">{{ formatDay(event.at) }}</p>
          <p class="who">
            {{ formatWhen(event.at) }}
            · {{ event.dinoName }}
            · {{ event.kind }}
            <template v-if="event.eventLabel"> · {{ event.eventLabel }}</template>
          </p>
          <p>{{ event.finalText }}</p>
          <TraceRow :trace="event" />
        </article>
      </div>
    </section>
  </div>
</template>

<style scoped>
.trace-status { display: flex; flex-direction: column; align-items: flex-end; gap: 0.2rem; }
.trace-page { margin: 0.8rem; min-height: calc(100vh - 7rem); }
.trace-page .hint { color: var(--paper-muted); }
.day-rule {
  margin: 0.2rem 0;
  font-size: 0.72rem;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--paper-muted);
}
</style>
