<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import LabWizard from "../components/LabWizard.vue";
import PanelInfo from "../components/PanelInfo.vue";
import { loadBundledPersonas } from "../data/loadPersonas";
import { recordTraces } from "../logic/trace";
import { decorateDinobook } from "../trace/decorate";
import TraceRow from "../trace/TraceRow.vue";
import { dayKey, formatDay, formatWhen } from "../trace/time";
import { useTrace } from "../trace/useTrace";
import {
  clampShiftPay,
  createLocalCore,
  creditPayday,
  DINOBOOK_EVENTS,
  LAB_TREE,
  LAB_FACTORIES,
  runDinobookEvent,
  seedWageCards,
  type AllChatLine,
  type DinobookCorePort,
  type DinobookEventId,
  type DinobookPost,
  type FamilyBrief,
  type Skip,
  type WageCard,
} from "./index";
import { bindPort, unboundPort } from "./portGate";

const personas = loadBundledPersonas();
const roster = personas.map((persona) => ({
  id: persona.id,
  name: persona.name,
  alignment: persona.alignment,
  color: persona.color,
}));

const binding = unboundPort();
const core = ref<DinobookCorePort | null>(null);
const portReady = ref(false);
const playerName = defineModel<string>("playerName", { default: "" });
const { enabled: tracesOn, byId: traceById } = useTrace();
const familyId = ref(LAB_TREE.families[0]?.familyId ?? "swamp");
const briefs = ref<FamilyBrief[]>([]);
const wages = ref<WageCard[]>([]);
/** Shift pay is not stored on a work row. It stays here until Payday credits it. */
const shiftPay = ref<Record<string, number>>({});
interface FeedRun {
  runId: string;
  eventId: DinobookEventId;
  label: string;
  at: string;
  posts: DinobookPost[];
  allChat: AllChatLine[];
  summary: string;
  skipped: Skip[];
}
const runs = ref<FeedRun[]>([]);
const memory = ref<Record<string, string>>({});
const idStart = ref(0);
const posting = ref(false);
const maxSpeakers = ref(4);
const maxChatLines = ref(1);
const maxChatTokens = ref(80);
const leaderOnly = ref(true);

function rootsOf(list: DinobookPost[]): DinobookPost[] {
  return list.filter((post) => post.link === "root" || !post.parentId);
}

function childrenOf(list: DinobookPost[], parentId: string): DinobookPost[] {
  return list.filter((post) => post.parentId === parentId);
}

function showDay(index: number): boolean {
  const run = runs.value[index];
  if (!run) return false;
  if (index === 0) return true;
  const prev = runs.value[index - 1];
  return !prev || dayKey(run.at) !== dayKey(prev.at);
}

function familyName(id: string | null): string {
  if (!id) return "Outside";
  return LAB_TREE.families.find((family) => family.familyId === id)?.name ?? id;
}

function boundCore(): DinobookCorePort | null {
  if (!portReady.value) return null;
  return core.value;
}

function syncBriefs() {
  const bound = boundCore();
  if (!bound) return;
  briefs.value = bound.briefs(playerName.value);
  const shifts = shiftPay.value;
  wages.value = bound.wages(playerName.value).map((card) => {
    const saved = shifts[card.dinoId];
    return saved === undefined ? card : { ...card, shiftPay: saved };
  });
}

watch(playerName, () => syncBriefs());

const ledgerLocked = computed(() => !portReady.value || core.value?.source === "src/core");
const coreNote = computed(() => {
  const bound = boundCore();
  if (!bound) return "";
  return bound.source === "src/core"
    ? "The core is the only record. Seed lab crew writes the demo. Payday uses creditWages. Lifetime only goes up."
    : "No src/core in this build.";
});

function setNumber(brief: FamilyBrief, field: keyof FamilyBrief, raw: string) {
  const bound = boundCore();
  if (!bound || bound.source === "src/core") return;
  const value = Number(raw);
  bound.setBrief(playerName.value, { ...brief, [field]: Number.isFinite(value) ? value : 0 });
  syncBriefs();
}

function setShift(card: WageCard, raw: string) {
  const value = Number(raw);
  shiftPay.value = {
    ...shiftPay.value,
    [card.dinoId]: clampShiftPay(Number.isFinite(value) ? value : 0),
  };
  syncBriefs();
}

function setFactory(card: WageCard, factoryId: string) {
  const bound = boundCore();
  if (!bound) return;
  bound.setWage(playerName.value, {
    ...card,
    factoryId,
    shiftPay: shiftPay.value[card.dinoId] ?? card.shiftPay,
  });
  syncBriefs();
}

function setEmployment(card: WageCard, raw: string) {
  const bound = boundCore();
  if (!bound) return;
  bound.setWage(playerName.value, {
    ...card,
    employment: raw === "rented" ? "rented" : "tamed",
    shiftPay: shiftPay.value[card.dinoId] ?? card.shiftPay,
  });
  syncBriefs();
}

function seedCrew() {
  const bound = boundCore();
  if (!bound) return;
  bound.seedLabCrew(playerName.value);
  const next = { ...shiftPay.value };
  for (const card of seedWageCards()) next[card.dinoId] = card.shiftPay;
  shiftPay.value = next;
  syncBriefs();
}

function dinoName(id: string): string {
  return roster.find((dino) => dino.id === id)?.name ?? id;
}

async function run(event: DinobookEventId) {
  const bound = boundCore();
  if (posting.value || !bound) return;
  posting.value = true;
  try {
  if (event !== "summon-big-grumble" && event !== "payday") {
    bound.apply(playerName.value, familyId.value, event);
    syncBriefs();
  }
  const earlierPosts = runs.value.flatMap((row) => row.posts);
  const earlierChat = runs.value.flatMap((row) => row.allChat);
  const result = runDinobookEvent({
    event,
    playerName: playerName.value,
    roster,
    briefs: briefs.value,
    wages: wages.value,
    memory: memory.value,
    idStart: idStart.value,
    priorTexts: [
      ...earlierPosts.map((post) => post.content),
      ...earlierChat.map((line) => line.text),
    ],
    policy: { maxSpeakersPerEvent: clampCount(maxSpeakers.value, 1, 8) },
    chat: {
      maxLines: clampCount(maxChatLines.value, 0, 4),
      maxTokens: clampCount(maxChatTokens.value, 0, 80),
      leaderOnly: leaderOnly.value,
    },
  });
  const traces = await decorateDinobook(result.traces, personas);
  recordTraces(traces);
  memory.value = result.memory;
  idStart.value = result.nextId;
  const leader = roster.find((dino) => dino.id === result.leaderId)?.name ?? result.leaderId;
  const approach = result.approach ? ` · Big Grumble ${result.approach} (${result.approachReason})` : "";
  const label = DINOBOOK_EVENTS.find((row) => row.id === event)?.label ?? event;
  runs.value = [...runs.value, {
    runId: result.runId,
    eventId: event,
    label,
    at: result.posts[0]?.at ?? result.allChat[0]?.at ?? new Date().toISOString(),
    posts: result.posts,
    allChat: result.allChat,
    summary: `${result.planId} · leader ${leader}${approach}`,
    skipped: result.skipped,
  }];
  for (const [dinoId, line] of Object.entries(result.memory)) {
    bound.remember(dinoId, line);
  }
  if (event === "payday") {
    creditPayday(bound, playerName.value, wages.value);
    for (const post of result.posts) {
      if (post.role === "mineral-find") bound.noteMineral(playerName.value, post.authorId);
    }
    syncBriefs();
  }
  } finally {
    posting.value = false;
  }
}

function clampCount(value: number, min: number, max: number): number {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, Math.floor(value)));
}

function resetFeed() {
  runs.value = [];
  memory.value = {};
  idStart.value = 0;
}

function resetStats() {
  const bound = boundCore();
  if (!bound) return;
  shiftPay.value = {};
  if (bound.source === "local") core.value = createLocalCore();
  syncBriefs();
}

function linkLabel(post: DinobookPost): string {
  const kind: Partial<Record<DinobookPost["role"], string>> = {
    "brag-pay": "brag",
    "mineral-find": "mineral",
    "complain-work": "complaint",
    "job-seeking": "job",
    report: "elder reply",
    verdict: "verdict",
  };
  const name = kind[post.role];
  if (post.link === "side") return name ? `side link · ${name}` : "side link";
  if (post.link === "child") return name ? `family · ${name}` : "family";
  return name ?? "elder";
}

onMounted(async () => {
  const port = await bindPort(binding);
  core.value = port;
  portReady.value = true;
  syncBriefs();
});

const lastSummary = computed(() => runs.value.at(-1)?.summary ?? "");
</script>

<template>
  <section class="book" aria-label="Dinobook">
    <header class="book-top">
      <div>
        <p class="eyebrow">Feed</p>
        <h2>Dinobook</h2>
      </div>
      <p class="hint book-note">
        Lab tree, not game canon. Elders post, families reply under them, friends sit on side links.
        Dinos read the feed before anyone speaks in all-chat.
      </p>
    </header>

    <div class="book-guides">
      <PanelInfo id="dinobook" />
      <div class="guide-pair">
        <LabWizard id="crew" />
        <LabWizard id="events" />
      </div>
    </div>

    <div class="book-grid">
      <div class="book-controls">
        <p class="hint">Player name is the one in Talk. A blank name is you.</p>
        <label class="field">
          <span>Stat bump applies to</span>
          <select v-model="familyId">
            <option v-for="family in LAB_TREE.families" :key="family.familyId" :value="family.familyId">
              {{ family.name }}
            </option>
          </select>
        </label>

        <div class="event-row" role="group" aria-label="Dinobook events">
          <button
            v-for="event in DINOBOOK_EVENTS"
            :key="event.id"
            type="button"
            :disabled="posting || !portReady"
            @click="run(event.id)"
          >
            {{ event.label }}
          </button>
        </div>

        <h3>Tree</h3>
        <ul class="tree">
          <li v-for="family in LAB_TREE.families" :key="family.familyId">
            <strong>{{ family.name }}</strong>
            <span v-for="memberId in family.memberIds" :key="memberId">
              {{ roster.find((dino) => dino.id === memberId)?.name ?? memberId }}
              <template v-if="memberId === family.elderId"> · elder</template>
            </span>
          </li>
          <li>
            <strong>Side links</strong>
            <span v-for="(link, index) in LAB_TREE.sideLinks" :key="index">
              {{ roster.find((dino) => dino.id === link.a)?.name }} – {{ roster.find((dino) => dino.id === link.b)?.name }}
            </span>
          </li>
        </ul>

        <h3>All-chat budget</h3>
        <div class="row">
          <label class="field">
            <span>Lines</span>
            <input v-model.number="maxChatLines" type="number" min="0" max="4" />
          </label>
          <label class="field">
            <span>Tokens</span>
            <input v-model.number="maxChatTokens" type="number" min="0" max="80" />
          </label>
          <label class="field">
            <span>Speakers</span>
            <input v-model.number="maxSpeakers" type="number" min="1" max="8" />
          </label>
        </div>
        <label class="check">
          <input v-model="leaderOnly" type="checkbox" />
          <span>Only the leader speaks in all-chat</span>
        </label>
        <p class="hint">Default is one line, the leader's demand. Posts stay at about 80 tokens.</p>

        <h3>Wages</h3>
        <p class="hint">Payday calls creditWages for the shift. Lifetime is the core total and only goes up. Mate rank is the crew place from mateRank: 1 is first pick. A blank factory means they are looking. Rented and tamed both count.</p>
        <button type="button" :disabled="!portReady" @click="seedCrew">Seed lab crew</button>
        <p class="hint" v-if="wages.length === 0">No payroll in the core yet.</p>
        <div v-for="card in wages" :key="card.dinoId" class="stat-card">
          <p class="who">{{ dinoName(card.dinoId) }} · mate rank {{ card.mateRank }}</p>
          <div class="stat-grid">
            <label>Shift <input :value="card.shiftPay" type="number" min="0" max="10000" @change="setShift(card, ($event.target as HTMLInputElement).value)" /></label>
            <label>Lifetime <input :value="card.lifetimeEarnings" type="number" readonly tabindex="-1" aria-readonly="true" /></label>
            <label class="span-2">Factory
              <select :value="card.factoryId" @change="setFactory(card, ($event.target as HTMLSelectElement).value)">
                <option value="">Looking</option>
                <option v-for="factory in LAB_FACTORIES" :key="factory.id" :value="factory.id">{{ factory.name }}</option>
              </select>
            </label>
            <label v-if="card.factoryId" class="span-2">Employment
              <select :value="card.employment ?? 'tamed'" @change="setEmployment(card, ($event.target as HTMLSelectElement).value)">
                <option value="tamed">Tamed</option>
                <option value="rented">Rented</option>
              </select>
            </label>
          </div>
        </div>

        <h3>What elders would tell Big Grumble</h3>
        <p class="hint" v-if="briefs.length === 0">No family rows in the core yet.</p>
        <div v-for="brief in briefs" :key="brief.familyId" class="stat-card">
          <p class="who">{{ familyName(brief.familyId) }} · {{ brief.band }} · rep {{ brief.rep }}</p>
          <div class="stat-grid">
            <label>Rep <input :value="brief.rep" type="number" min="-100" max="100" :disabled="ledgerLocked" @change="setNumber(brief, 'rep', ($event.target as HTMLInputElement).value)" /></label>
            <label>Kills <input :value="brief.kills" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'kills', ($event.target as HTMLInputElement).value)" /></label>
            <label>Elder kills <input :value="brief.elderKills" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'elderKills', ($event.target as HTMLInputElement).value)" /></label>
            <label>Bounties <input :value="brief.bountiesOnPlayer" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'bountiesOnPlayer', ($event.target as HTMLInputElement).value)" /></label>
            <label>Claimed <input :value="brief.bountiesClaimed" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'bountiesClaimed', ($event.target as HTMLInputElement).value)" /></label>
            <label>Rentals down <input :value="brief.rentalsDowned" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'rentalsDowned', ($event.target as HTMLInputElement).value)" /></label>
            <label>Mango refused <input :value="brief.mangoRefused" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'mangoRefused', ($event.target as HTMLInputElement).value)" /></label>
            <label>Mango given <input :value="brief.mangoGiven" type="number" min="0" :disabled="ledgerLocked" @change="setNumber(brief, 'mangoGiven', ($event.target as HTMLInputElement).value)" /></label>
          </div>
        </div>
        <div class="row">
          <button type="button" @click="resetFeed">Clear feed</button>
          <button type="button" :disabled="!portReady" @click="resetStats">Reset stats</button>
        </div>
        <p class="hint">Rep bands come from REP_BAND_THRESHOLDS: friendly at 20, wary at -8, hostile at -40. {{ coreNote }}</p>
      </div>

      <div class="book-feed">
        <div class="log-head">
          <h3>Thread</h3>
          <PanelInfo id="dinobook-feed" />
          <p class="hint" v-if="lastSummary">{{ lastSummary }}</p>
        </div>
        <div class="sheet" role="feed">
          <p class="hint" v-if="runs.length === 0">Trigger an event. The feed fills before all-chat.</p>
          <template v-for="(run, index) in runs" :key="run.runId">
            <p v-if="showDay(index)" class="day-rule">{{ formatDay(run.at) }}</p>
            <header class="event-head">
              <strong>{{ run.label }}</strong>
              <time :datetime="run.at">{{ formatWhen(run.at) }}</time>
              <span>{{ run.summary }}</span>
            </header>
            <article v-for="post in rootsOf(run.posts)" :key="post.id" class="thread">
              <div class="bubble" :style="{ borderColor: post.color }">
                <p class="who">{{ post.authorName }} · {{ linkLabel(post) }} · {{ formatWhen(post.at) }} · {{ post.tokens }} tok · {{ post.role }}</p>
                <p>{{ post.content }}</p>
                <TraceRow v-if="tracesOn && post.traceId" :trace="traceById(post.traceId)" />
              </div>
              <article
                v-for="child in childrenOf(run.posts, post.id)"
                :key="child.id"
                class="bubble nested"
                :class="{ side: child.link === 'side' }"
                :style="{ borderColor: child.color }"
              >
                <p class="who">
                  {{ child.authorName }} · {{ linkLabel(child) }}
                  <template v-if="child.link === 'side'"> · friendly</template>
                  · {{ formatWhen(child.at) }} · {{ child.tokens }} tok · {{ child.role }}
                </p>
                <p>{{ child.content }}</p>
                <TraceRow v-if="tracesOn && child.traceId" :trace="traceById(child.traceId)" />
                <article
                  v-for="nested in childrenOf(run.posts, child.id)"
                  :key="nested.id"
                  class="bubble nested"
                  :class="{ side: nested.link === 'side' }"
                  :style="{ borderColor: nested.color }"
                >
                  <p class="who">{{ nested.authorName }} · {{ linkLabel(nested) }} · {{ formatWhen(nested.at) }} · {{ nested.tokens }} tok</p>
                  <p>{{ nested.content }}</p>
                  <TraceRow v-if="tracesOn && nested.traceId" :trace="traceById(nested.traceId)" />
                </article>
              </article>
            </article>
          </template>
        </div>

        <div class="allchat">
          <h3>All chat</h3>
          <p class="hint" v-if="runs.length === 0">Quiet. The budget is holding the rest on Dinobook.</p>
          <template v-for="run in runs" :key="`${run.runId}-chat`">
            <p class="event-label">{{ run.label }} · {{ formatWhen(run.at) }}</p>
            <p class="hint" v-if="run.allChat.length === 0">Quiet for this event. The budget is holding the rest on Dinobook.</p>
            <article v-for="line in run.allChat" :key="line.id" class="bubble" :style="{ borderColor: line.color }">
              <p class="who">{{ line.speakerName }} · {{ line.reason }} · {{ formatWhen(line.at) }} · {{ line.tokens }} tok</p>
              <p>{{ line.text }}</p>
              <TraceRow v-if="tracesOn && line.traceId" :trace="traceById(line.traceId)" />
            </article>
            <p class="hint" v-if="run.skipped.length">
              Held back:
              <span v-for="skip in run.skipped" :key="skip.dinoId + skip.reason">{{ skip.name }} ({{ skip.reason }}) </span>
            </p>
          </template>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.book {
  grid-column: 1 / -1;
  border: 1px solid var(--line);
  border-radius: 14px;
  background: var(--slate);
  color: var(--ink);
}
.book-top {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  align-items: end;
  padding: 0.85rem 0.9rem 0.4rem;
}
.book-top h2 { margin: 0.1rem 0 0; font-size: 1.15rem; }
.book-note { max-width: 46rem; text-align: right; }
.book-grid {
  display: grid;
  grid-template-columns: minmax(240px, 340px) minmax(0, 1fr);
  gap: 0.8rem;
  padding: 0.4rem 0.8rem 0.9rem;
}
.book-controls h3, .book-feed h3 { margin: 0.8rem 0 0.35rem; font-size: 0.92rem; }
.book-controls .who { color: var(--muted); }
.event-row { display: flex; flex-wrap: wrap; gap: 0.35rem; margin-bottom: 0.6rem; }
.tree { list-style: none; margin: 0; padding: 0; }
.tree li { margin: 0.35rem 0 0.55rem; }
.tree span, .tree strong { display: block; }
.tree span { color: var(--muted); font-size: 0.82rem; }
.stat-card {
  border: 1px solid var(--line);
  border-radius: 10px;
  padding: 0.45rem 0.55rem;
  margin-bottom: 0.45rem;
  background: var(--slate-2);
}
.stat-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.35rem 0.5rem;
}
.span-2 { grid-column: 1 / -1; }
.stat-grid label {
  display: flex;
  flex-direction: column;
  gap: 0.15rem;
  font-size: 0.75rem;
  color: var(--muted);
}
.stat-grid input, .stat-grid select { width: 100%; }
.stat-grid input[readonly] { background: transparent; color: var(--ink); }
.book-feed {
  background: var(--paper);
  color: var(--paper-ink);
  border-radius: 12px;
  min-height: 28rem;
  display: flex;
  flex-direction: column;
}
.book-feed .log-head { padding: 0.75rem 0.9rem 0.3rem; }
.book-feed .sheet { max-height: 34rem; }
.thread { margin-bottom: 0.8rem; }
.nested { margin-left: 1.1rem; }
.side { border-left-style: dashed; }
.allchat {
  border-top: 1px solid var(--rule);
  padding: 0.7rem 0.9rem 0.9rem;
}
.allchat .hint, .book-feed .hint, .event-label { color: var(--paper-muted); }
.event-label { margin: 0.55rem 0 0.15rem; font-size: 0.78rem; }
@media (max-width: 980px) {
  .book-top { flex-direction: column; align-items: start; }
  .book-note { text-align: left; }
  .book-grid { grid-template-columns: 1fr; }
  .book { order: 4; }
}
</style>
