<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { loadBundledPersonas } from "../data/loadPersonas";
import {
  clampShiftPay,
  createLocalCore,
  creditPayday,
  DINOBOOK_EVENTS,
  LAB_TREE,
  LAB_FACTORIES,
  loadCorePort,
  runDinobookEvent,
  seedWageCards,
  type AllChatLine,
  type DinobookCorePort,
  type DinobookEventId,
  type DinobookPost,
  type FamilyBrief,
  type RunResult,
  type WageCard,
} from "./index";

const personas = loadBundledPersonas();
const roster = personas.map((persona) => ({
  id: persona.id,
  name: persona.name,
  alignment: persona.alignment,
  color: persona.color,
}));

const core = ref<DinobookCorePort>(createLocalCore());
const playerName = ref("Pip");
const familyId = ref(LAB_TREE.families[0]?.familyId ?? "swamp");
const briefs = ref<FamilyBrief[]>([]);
const wages = ref<WageCard[]>([]);
/** Shift pay is not stored on a work row. It stays here until Payday credits it. */
const shiftPay = ref<Record<string, number>>({});
const posts = ref<DinobookPost[]>([]);
const chatLines = ref<AllChatLine[]>([]);
const last = ref<RunResult | null>(null);
const memory = ref<Record<string, string>>({});
const idStart = ref(0);
const maxSpeakers = ref(4);
const maxChatLines = ref(1);
const maxChatTokens = ref(80);
const leaderOnly = ref(true);

const roots = computed(() => posts.value.filter((post) => post.link === "root" || !post.parentId));

function childrenOf(parentId: string): DinobookPost[] {
  return posts.value.filter((post) => post.parentId === parentId);
}

function familyName(id: string | null): string {
  if (!id) return "Outside";
  return LAB_TREE.families.find((family) => family.familyId === id)?.name ?? id;
}

function syncBriefs() {
  briefs.value = core.value.briefs(playerName.value);
  const shifts = shiftPay.value;
  wages.value = core.value.wages(playerName.value).map((card) => {
    const saved = shifts[card.dinoId];
    return saved === undefined ? card : { ...card, shiftPay: saved };
  });
}

function onPlayer() {
  syncBriefs();
}

const ledgerLocked = computed(() => core.value.source === "src/core");

function setNumber(brief: FamilyBrief, field: keyof FamilyBrief, raw: string) {
  if (ledgerLocked.value) return;
  const value = Number(raw);
  core.value.setBrief(playerName.value, { ...brief, [field]: Number.isFinite(value) ? value : 0 });
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
  core.value.setWage(playerName.value, {
    ...card,
    factoryId,
    shiftPay: shiftPay.value[card.dinoId] ?? card.shiftPay,
  });
  syncBriefs();
}

function setEmployment(card: WageCard, raw: string) {
  core.value.setWage(playerName.value, {
    ...card,
    employment: raw === "rented" ? "rented" : "tamed",
    shiftPay: shiftPay.value[card.dinoId] ?? card.shiftPay,
  });
  syncBriefs();
}

function seedCrew() {
  core.value.seedLabCrew(playerName.value);
  const next = { ...shiftPay.value };
  for (const card of seedWageCards()) next[card.dinoId] = card.shiftPay;
  shiftPay.value = next;
  syncBriefs();
}

function dinoName(id: string): string {
  return roster.find((dino) => dino.id === id)?.name ?? id;
}

function run(event: DinobookEventId) {
  if (event !== "summon-big-grumble" && event !== "payday") {
    core.value.apply(playerName.value, familyId.value, event);
    syncBriefs();
  }
  const result = runDinobookEvent({
    event,
    playerName: playerName.value,
    roster,
    briefs: briefs.value,
    wages: wages.value,
    memory: memory.value,
    idStart: idStart.value,
    priorTexts: [
      ...posts.value.map((post) => post.content),
      ...chatLines.value.map((line) => line.text),
    ],
    policy: { maxSpeakersPerEvent: clampCount(maxSpeakers.value, 1, 8) },
    chat: {
      maxLines: clampCount(maxChatLines.value, 0, 4),
      maxTokens: clampCount(maxChatTokens.value, 0, 80),
      leaderOnly: leaderOnly.value,
    },
  });
  posts.value = [...posts.value, ...result.posts];
  chatLines.value = [...chatLines.value, ...result.allChat];
  memory.value = result.memory;
  idStart.value = result.nextId;
  last.value = result;
  for (const [dinoId, line] of Object.entries(result.memory)) {
    core.value.remember(dinoId, line);
  }
  if (event === "payday") {
    creditPayday(core.value, playerName.value, wages.value);
    for (const post of result.posts) {
      if (post.role === "mineral-find") core.value.noteMineral(playerName.value, post.authorId);
    }
    syncBriefs();
  }
}

function clampCount(value: number, min: number, max: number): number {
  if (!Number.isFinite(value)) return min;
  return Math.min(max, Math.max(min, Math.floor(value)));
}

function resetFeed() {
  posts.value = [];
  chatLines.value = [];
  last.value = null;
  memory.value = {};
  idStart.value = 0;
}

function resetStats() {
  shiftPay.value = {};
  if (core.value.source === "local") core.value = createLocalCore();
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
  core.value = await loadCorePort();
  syncBriefs();
});

const lastChat = computed(() => (last.value ? last.value.allChat : []));
const lastSkipped = computed(() => (last.value ? last.value.skipped : []));
const lastSummary = computed(() => {
  const current = last.value;
  if (!current) return "";
  const name = roster.find((dino) => dino.id === current.leaderId)?.name ?? current.leaderId;
  const approach = current.approach ? ` · Big Grumble ${current.approach} (${current.approachReason})` : "";
  return `${current.planId} · leader ${name}${approach}`;
});
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

    <div class="book-grid">
      <div class="book-controls">
        <label class="field">
          <span>Player</span>
          <input v-model="playerName" type="text" maxlength="40" @change="onPlayer" />
        </label>
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
        <button type="button" @click="seedCrew">Seed lab crew</button>
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
          <button type="button" @click="resetStats">Reset stats</button>
        </div>
        <p class="hint">Rep bands come from REP_BAND_THRESHOLDS: friendly at 20, wary at -8, hostile at -40. {{ core.source === "src/core" ? "The core is the only record. Seed lab crew writes the demo. Payday uses creditWages. Lifetime only goes up." : "No src/core in this build." }}</p>
      </div>

      <div class="book-feed">
        <div class="log-head">
          <h3>Thread</h3>
          <p class="hint" v-if="lastSummary">{{ lastSummary }}</p>
        </div>
        <div class="sheet" role="feed">
          <p class="hint" v-if="posts.length === 0">Trigger an event. The feed fills before all-chat.</p>
          <article v-for="post in roots" :key="post.id" class="thread">
            <div class="bubble" :style="{ borderColor: post.color }">
              <p class="who">{{ post.authorName }} · {{ linkLabel(post) }} · {{ post.tokens }} tok · {{ post.role }}</p>
              <p>{{ post.content }}</p>
            </div>
            <article
              v-for="child in childrenOf(post.id)"
              :key="child.id"
              class="bubble nested"
              :class="{ side: child.link === 'side' }"
              :style="{ borderColor: child.color }"
            >
              <p class="who">
                {{ child.authorName }} · {{ linkLabel(child) }}
                <template v-if="child.link === 'side'"> · friendly</template>
                · {{ child.tokens }} tok · {{ child.role }}
              </p>
              <p>{{ child.content }}</p>
              <article
                v-for="nested in childrenOf(child.id)"
                :key="nested.id"
                class="bubble nested"
                :class="{ side: nested.link === 'side' }"
                :style="{ borderColor: nested.color }"
              >
                <p class="who">{{ nested.authorName }} · {{ linkLabel(nested) }} · {{ nested.tokens }} tok</p>
                <p>{{ nested.content }}</p>
              </article>
            </article>
          </article>
        </div>

        <div class="allchat">
          <h3>All chat this event</h3>
          <p class="hint" v-if="lastChat.length === 0">Quiet. The budget is holding the rest on Dinobook.</p>
          <article v-for="line in lastChat" :key="line.id" class="bubble" :style="{ borderColor: line.color }">
            <p class="who">{{ line.speakerName }} · {{ line.reason }} · {{ line.tokens }} tok</p>
            <p>{{ line.text }}</p>
          </article>
          <p class="hint" v-if="lastSkipped.length">
            Held back:
            <span v-for="skip in lastSkipped" :key="skip.dinoId + skip.reason">{{ skip.name }} ({{ skip.reason }}) </span>
          </p>
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
.allchat .hint, .book-feed .hint { color: var(--paper-muted); }
@media (max-width: 980px) {
  .book-top { flex-direction: column; align-items: start; }
  .book-note { text-align: left; }
  .book-grid { grid-template-columns: 1fr; }
  .book { order: 4; }
}
</style>
