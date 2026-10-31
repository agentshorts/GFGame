<script setup lang="ts">
import { useLab } from "./composables/useLab";
import LabWizard from "./components/LabWizard.vue";
import PanelInfo from "./components/PanelInfo.vue";
import DinobookPanel from "./dinobook/DinobookPanel.vue";
import TraceRow from "./trace/TraceRow.vue";
import TraceToggle from "./trace/TraceToggle.vue";
import { useTrace } from "./trace/useTrace";

const {
  personas,
  alignments,
  lineEngines,
  mode,
  engineId,
  selectedId,
  checkedIds,
  playerName,
  temperature,
  maxTokens,
  systemPrompt,
  promptDirty,
  showFilter,
  messages,
  draft,
  busy,
  notice,
  inventory,
  distanceFt,
  smellRangeFt,
  winOdds,
  dinobook,
  alignmentPick,
  knobs,
  contextBudget,
  allChatBudget,
  activeBudget,
  contextTokens,
  contextSummary,
  contextWorld,
  playerRecord,
  relationshipSummary,
  relationshipKind,
  relationshipKinds,
  statKinds,
  elderOn,
  familyId,
  decisions,
  focusDecision,
  previewSteer,
  selected,
  webGpu,
  modelChoices,
  modelMissing,
  modelId,
  currentModel,
  loadProgress,
  modelError,
  modelLoading,
  loadModel,
  loadedModelId,
  fromCache,
  lastStats,
  steerWithModel,
  rephraseWithModel,
  send,
  anotherRound,
  resetChat,
  exportTranscript,
  addItem,
  addMango,
  removeItem,
  applyScene,
  toggleChecked,
  markPromptDirty,
  rebuildPrompt,
  onBudget,
  onAllChatBudget,
  patchQuota,
  patchThresholds,
  setSummarizeEvery,
  setFamily,
  setRepDelta,
  toggleElder,
  setRelationshipKind,
  recordWorld,
  colorFor,
  usePreset,
} = useLab();

const { enabled: tracesOn, byId: traceById } = useTrace();

const presets = [
  "why you running",
  "player ran away",
  "player attacked Big Grumble",
  "new player joined",
];

const meterPct = () => Math.min(100, Math.round((contextTokens.value / Math.max(1, activeBudget.value)) * 100));

function numberFrom(event: Event): number {
  return Number((event.target as HTMLInputElement).value);
}

function onKind(event: Event) {
  setRelationshipKind((event.target as HTMLSelectElement).value as Parameters<typeof setRelationshipKind>[0]);
}

function onComposerKey(event: KeyboardEvent) {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    void send();
  }
}

function onPromptInput(event: Event) {
  markPromptDirty();
  systemPrompt.value = (event.target as HTMLTextAreaElement).value;
}
</script>

<template>
  <div class="lab">
    <header class="top">
      <div>
        <p class="eyebrow">Workbench</p>
        <h1>Dino Chat Lab</h1>
        <a class="lab-link" href="#/personas">Personas</a>
        <a class="lab-link" href="#/trace">Trace log</a>
        <a class="lab-link" href="#/help">Help</a>
      </div>
      <div class="status">
        <TraceToggle />
        <p>
          {{ lineEngines.find((choice) => choice.id === engineId)?.label }}
          · context {{ contextTokens }}/{{ activeBudget }}
          · intent {{ focusDecision.intent }}
        </p>
      </div>
    </header>
    <div class="columns">
      <section class="bench" aria-label="Chat controls">
        <h2>Talk</h2>
        <PanelInfo id="talk" />
        <div class="modes" role="group" aria-label="Chat mode">
          <button type="button" :aria-pressed="mode === 'direct'" @click="mode = 'direct'">1:1</button>
          <button type="button" :aria-pressed="mode === 'all-chat'" @click="mode = 'all-chat'">All-chat</button>
          <button type="button" :aria-pressed="mode === 'event'" @click="mode = 'event'">Game event</button>
        </div>
        <label class="field">
          <span>Line engine</span>
          <select v-model="engineId">
            <option v-for="choice in lineEngines" :key="choice.id" :value="choice.id">
              {{ choice.label }}
            </option>
          </select>
          <small class="hint">{{ lineEngines.find((choice) => choice.id === engineId)?.blurb }} Temperature is for the context model. These engines ignore it.</small>
        </label>
        <label class="field">
          <span>Focus dino</span>
          <select v-model="selectedId">
            <option v-for="persona in personas" :key="persona.id" :value="persona.id">
              {{ persona.name }} · {{ persona.alignment }}
            </option>
          </select>
        </label>
        <div v-if="mode === 'all-chat'" class="field">
          <span>On the channel</span>
          <div v-for="persona in personas" :key="persona.id" class="persona-line">
            <input
              type="checkbox"
              :checked="checkedIds.includes(persona.id)"
              :aria-label="`Include ${persona.name}`"
              @change="toggleChecked(persona.id)"
            />
            <i class="swatch" :style="{ background: persona.color }"></i>
            <button type="button" class="grow" @click="selectedId = persona.id">{{ persona.name }}</button>
          </div>
        </div>
        <label class="field">
          <span>Player name</span>
          <input v-model="playerName" type="text" maxlength="40" placeholder="Blank leaves {Target} as you" />
        </label>
        <div class="row">
          <label class="field">
            <span>Temperature {{ temperature.toFixed(2) }}</span>
            <input v-model.number="temperature" type="range" min="0" max="1.5" step="0.05" />
          </label>
          <label class="field">
            <span>Max tokens</span>
            <input v-model.number="maxTokens" type="number" min="1" max="512" />
          </label>
        </div>
        <label class="field">
          <span>System prompt</span>
          <textarea :value="systemPrompt" @input="onPromptInput"></textarea>
        </label>
        <p class="hint" v-if="promptDirty">Edited. Rebuild to pick up the live alignment and memory.</p>
        <div class="row">
          <button type="button" @click="rebuildPrompt">Rebuild prompt</button>
          <button type="button" @click="resetChat">Reset chat</button>
          <button type="button" @click="exportTranscript">Export JSON</button>
        </div>
        <label class="check">
          <input v-model="showFilter" type="checkbox" />
          <span>Show what the PG filter changed</span>
        </label>

        <h2>Context model</h2>
        <p class="hint" :class="webGpu.ok ? 'good' : 'bad'">{{ webGpu.detail }}</p>
        <label class="field">
          <span>Small WebLLM prebuilt</span>
          <select v-model="modelId">
            <option v-for="choice in modelChoices" :key="choice.modelId" :value="choice.modelId">
              {{ choice.label }}
            </option>
          </select>
        </label>
        <p class="hint" v-if="currentModel">
          {{ currentModel.modelId }}
          <template v-if="currentModel.vramMb"> · about {{ Math.round(currentModel.vramMb) }} MB VRAM</template>
          <template v-if="currentModel.lowResource"> · low resource</template>
        </p>
        <p class="hint" v-if="modelMissing.length">Missing from this WebLLM build: {{ modelMissing.join(", ") }}.</p>
        <button type="button" :disabled="modelLoading || !modelId" @click="loadModel">
          {{ modelLoading ? "Loading…" : "Load context model" }}
        </button>
        <div class="meter" style="margin-top: 0.45rem" v-if="modelLoading || loadProgress.text">
          <div :style="{ width: `${Math.round(loadProgress.progress * 100)}%` }"></div>
        </div>
        <p class="hint" v-if="loadProgress.text">{{ loadProgress.text }}</p>
        <p class="bad" v-if="modelError">{{ modelError }}</p>
        <p class="hint" v-if="loadedModelId">
          Loaded {{ loadedModelId }}{{ fromCache ? " from the browser cache" : "" }}.
          Weights stay in the browser cache, not in this repo.
        </p>
        <p class="hint" v-if="lastStats?.tokensPerSec">
          {{ lastStats.tokensPerSec.toFixed(1) }} tok/s
          <template v-if="lastStats.vramMb"> · {{ Math.round(lastStats.vramMb) }} MB VRAM</template>
          <template v-if="lastStats.runtimeStatsText"> · {{ lastStats.runtimeStatsText }}</template>
        </p>
        <label class="check">
          <input v-model="steerWithModel" type="checkbox" />
          <span>Context model steers the intent</span>
        </label>
        <label class="check">
          <input v-model="rephraseWithModel" type="checkbox" />
          <span>Rephrase the classic line</span>
        </label>
        <p class="hint">Both wait until a model is loaded. Until then the classic engine speaks the steered line.</p>
      </section>

      <section class="log" aria-label="Transcript">
        <div class="log-head">
          <h2>{{ mode === "event" ? "Game events" : mode === "all-chat" ? "All chat" : selected.name }}</h2>
          <p class="hint">Channel All. The classic engine writes the line. The slate panel is the decision.</p>
          <PanelInfo id="transcript" />
          <LabWizard id="traces" />
        </div>
        <div class="sheet" role="log">
          <p class="hint" v-if="messages.length === 0">
            Big Grumble starts outside smell range, so “why you running” stays a chat line.
          </p>
          <article
            v-for="message in messages"
            :key="message.seq"
            class="bubble"
            :style="{ borderColor: message.role === 'dino' ? colorFor(message.speaker) : '#8a7560' }"
          >
            <p class="who">{{ message.speaker }} · {{ message.role }}<template v-if="message.engine"> · {{ message.engine }}</template></p>
            <p>{{ message.text }}</p>
            <p class="meta" v-if="message.intent">
              intent {{ message.intent }}
              <template v-if="message.rephrased"> · rephrased</template>
              <template v-if="message.contextTokens"> · context {{ message.contextTokens }}/{{ message.contextBudget }}</template>
              <template v-if="message.stats?.tokensPerSec"> · {{ message.stats.tokensPerSec.toFixed(1) }} tok/s</template>
            </p>
            <p class="meta" v-if="message.steer">{{ message.steer }}</p>
            <p class="filter-note" v-if="showFilter && message.role === 'dino'">
              <template v-if="message.filtered">
                Filtered
                <span v-for="(swap, index) in message.replacements" :key="index">
                  “{{ swap.original }}” → {{ swap.replacement }}
                </span>
              </template>
              <template v-else>Nothing was filtered.</template>
            </p>
            <TraceRow v-if="tracesOn && message.traceId" :trace="traceById(message.traceId)" />
          </article>
        </div>
        <form class="composer" @submit.prevent="send">
          <div class="presets">
            <button v-for="preset in presets" :key="preset" type="button" @click="usePreset(preset)">{{ preset }}</button>
          </div>
          <textarea
            v-model="draft"
            :placeholder="mode === 'event' ? 'Game event' : 'Say something'"
            @keydown="onComposerKey"
          ></textarea>
          <p class="bad" v-if="notice">{{ notice }}</p>
          <div class="row">
            <button type="submit" :disabled="busy">{{ busy ? "Talking…" : "Send" }}</button>
            <button v-if="mode === 'all-chat'" type="button" :disabled="busy" @click="anotherRound">Another round</button>
          </div>
        </form>
      </section>

      <aside class="debug" aria-label="Alignment debug">
        <h2>Decision</h2>
        <PanelInfo id="decision" />
        <p class="hint">{{ selected.name }} · lab alignment {{ alignmentPick === "persona" ? selected.alignment : alignmentPick }}</p>
        <label class="field">
          <span>Alignment override</span>
          <select v-model="alignmentPick">
            <option value="persona">Persona default ({{ selected.alignment }})</option>
            <option v-for="alignment in alignments" :key="alignment" :value="alignment">{{ alignment }}</option>
          </select>
        </label>
        <p class="intent">{{ focusDecision.intent }}</p>
        <ol class="reasons">
          <li v-for="(reason, index) in focusDecision.reasons" :key="index">{{ reason }}</li>
        </ol>
        <p class="speech">Hint: {{ focusDecision.speechHint }}</p>
        <p class="hint" v-if="selected.exampleLinesCanon">
          Big Grumble still speaks a canned line from the giant-dino sheet. The hint is the lab sentence.
        </p>
        <p class="hint">Next classic utterance: {{ previewSteer.utterance || "(empty)" }}</p>
        <p class="todo">{{ focusDecision.todo }}</p>
        <ul v-if="mode === 'all-chat'" class="reasons">
          <li v-for="row in decisions" :key="row.persona.id">{{ row.persona.name }} · {{ row.decision.intent }}</li>
        </ul>

        <h2>Player</h2>
        <div v-for="(stack, index) in inventory" :key="index" class="stack">
          <input v-model="stack.id" aria-label="Item id" />
          <input v-model="stack.name" aria-label="Item name" />
          <input v-model.number="stack.count" type="number" min="0" aria-label="Count" />
          <button type="button" class="slim" @click="removeItem(index)">Remove</button>
        </div>
        <div class="row">
          <button type="button" @click="addMango">Add mango block</button>
          <button type="button" @click="addItem">Add item</button>
        </div>
        <div class="row">
          <label class="field">
            <span>Distance (ft)</span>
            <input v-model.number="distanceFt" type="number" min="0" max="500" />
          </label>
          <label class="field">
            <span>Smell (ft)</span>
            <input v-model.number="smellRangeFt" type="number" min="0" max="500" />
          </label>
        </div>
        <label class="field">
          <span>Odds of beating the player {{ winOdds.toFixed(2) }}</span>
          <input v-model.number="winOdds" type="range" min="0" max="1" step="0.05" />
        </label>
        <label class="check">
          <input v-model="dinobook" type="checkbox" />
          <span>Dinobook can rally help</span>
        </label>
        <div class="row">
          <button type="button" @click="applyScene('attack')">Tusksaw attack</button>
          <button type="button" @click="applyScene('ambush')">Tusksaw ambush</button>
          <button type="button" @click="applyScene('clear')">Clear world</button>
        </div>
        <p class="hint">Attack: mango ×3, 12 ft, odds 0.8, no Dinobook. Ambush: same pack, odds 0.2, Dinobook on.</p>

        <h2>Context budget</h2>
        <p class="hint">{{ contextTokens }} / {{ activeBudget }} on {{ selected.name }}. Each dino keeps a separate window.</p>
        <label class="field">
          <span>1:1 and event budget</span>
          <input
            :value="contextBudget"
            type="number"
            min="128"
            max="8192"
            step="128"
            @change="onBudget(numberFrom($event))"
          />
        </label>
        <label class="field">
          <span>All-chat budget</span>
          <input
            :value="allChatBudget"
            type="number"
            min="128"
            max="8192"
            step="128"
            @change="onAllChatBudget(numberFrom($event))"
          />
        </label>
        <div class="meter" role="meter" :aria-valuenow="contextTokens" :aria-valuemax="activeBudget" aria-label="Context tokens">
          <div :style="{ width: `${meterPct()}%` }"></div>
        </div>
        <p class="hint">Default 4096. Four characters count as one token here. A loaded model reports its own usage.</p>
        <label class="field">
          <span>Summarize every N turns</span>
          <input :value="knobs.summarizeEvery" type="number" min="1" max="100" @change="setSummarizeEvery(numberFrom($event))" />
        </label>
        <p class="hint" v-if="contextSummary">Summary: {{ contextSummary }}</p>
        <p class="hint" v-if="relationshipSummary">With this player: {{ relationshipSummary }}</p>
        <p class="hint" v-if="contextWorld">{{ contextWorld }}</p>
        <p class="hint" v-if="playerRecord">{{ elderOn ? "Elder record" : "Rep band" }}: {{ playerRecord }}</p>

        <h2>Cooldowns and thresholds</h2>
        <div class="row">
          <label class="field">
            <span>Rest (seconds)</span>
            <input :value="knobs.quota.restSeconds" type="number" min="0" max="86400" @change="patchQuota({ restSeconds: numberFrom($event) })" />
          </label>
          <label class="field">
            <span>Per minute</span>
            <input :value="knobs.quota.maxPerMinute" type="number" min="0" max="1000" @change="patchQuota({ maxPerMinute: numberFrom($event) })" />
          </label>
        </div>
        <div class="row">
          <label class="field">
            <span>Per hour</span>
            <input :value="knobs.quota.maxPerHour" type="number" min="0" max="10000" @change="patchQuota({ maxPerHour: numberFrom($event) })" />
          </label>
          <label class="field">
            <span>Same player / hour</span>
            <input :value="knobs.quota.maxToSamePlayerPerHour" type="number" min="0" max="10000" @change="patchQuota({ maxToSamePlayerPerHour: numberFrom($event) })" />
          </label>
        </div>
        <label class="field">
          <span>Speak chance {{ knobs.quota.speakChance.toFixed(2) }}</span>
          <input :value="knobs.quota.speakChance" type="number" min="0" max="1" step="0.05" @change="patchQuota({ speakChance: numberFrom($event) })" />
        </label>
        <div class="row">
          <label class="field">
            <span>Interest minimum</span>
            <input :value="knobs.thresholds.interestMin" type="number" min="1" max="10" @change="patchThresholds({ interestMin: numberFrom($event) })" />
          </label>
          <label class="field">
            <span>Reply-to-reply interest</span>
            <input :value="knobs.thresholds.replyToReplyInterest" type="number" min="1" max="10" @change="patchThresholds({ replyToReplyInterest: numberFrom($event) })" />
          </label>
        </div>
        <label class="field">
          <span>Relationship to this player</span>
          <select :value="relationshipKind" @change="onKind">
            <option v-for="kind in relationshipKinds" :key="kind" :value="kind">{{ kind }}</option>
          </select>
        </label>
        <label class="check">
          <input :checked="elderOn" type="checkbox" @change="toggleElder(($event.target as HTMLInputElement).checked)" />
          <span>Elder of this family (reads the player record)</span>
        </label>
        <label class="field">
          <span>Family id</span>
          <input :value="familyId" type="text" @change="setFamily(($event.target as HTMLInputElement).value)" />
        </label>
        <details>
          <summary>Rep deltas</summary>
          <div v-for="kind in statKinds" :key="kind" class="row delta">
            <span>{{ kind }}</span>
            <input :value="knobs.repDeltas[kind]" type="number" min="-100" max="100" @change="setRepDelta(kind, numberFrom($event))" />
          </div>
        </details>

        <h2>World events</h2>
        <p class="hint">These buttons write stat events for the current player name. They are not wired to Unreal.</p>
        <div class="pills">
          <button v-for="kind in statKinds" :key="kind" type="button" @click="recordWorld(kind)">{{ kind.replaceAll('_', ' ') }}</button>
        </div>
      </aside>
      <DinobookPanel v-model:player-name="playerName" />
    </div>
  </div>
</template>
