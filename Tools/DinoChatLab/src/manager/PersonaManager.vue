<script setup lang="ts">
import { ref } from "vue";
import { SPEECH_PATTERNS, type Sentiment } from "./profile";
import { useManager } from "./useManager";
import TraceRow from "../trace/TraceRow.vue";
import TraceToggle from "../trace/TraceToggle.vue";
import { useTrace } from "../trace/useTrace";
import "./manager.css";

const {
  mode,
  needsName,
  username,
  nameDraft,
  nameError,
  nameBusy,
  query,
  patternFilter,
  notice,
  playerName,
  draft,
  chatBusy,
  logs,
  dinoSummary,
  relationshipSummary,
  rep,
  earnings,
  repLabel,
  repBands,
  selected,
  editable,
  groups,
  chatLines,
  refreshForPlayer,
  submitName,
  select,
  addDino,
  duplicate,
  save,
  remove,
  markDirty,
  setPattern,
  setSpeechStyle,
  addTopic,
  removeTopic,
  addRelationship,
  setRelationshipKind,
  setAffinity,
  removeRelationship,
  setInterest,
  send,
  resetChat,
  resetMemory,
  download,
  onImport,
} = useManager();

const { enabled: tracesOn, byId: traceById } = useTrace();
const topicName = ref("");
const topicSentiment = ref<Sentiment>("like");
const relSubject = ref("");
const relKind = ref<"friend" | "rival" | "family">("friend");
const importInput = ref<HTMLInputElement | null>(null);

function addTopicFromForm() {
  addTopic(topicName.value, topicSentiment.value);
  topicName.value = "";
}

function addRelFromForm() {
  addRelationship(relSubject.value, relKind.value);
  relSubject.value = "";
}

function onComposerKey(event: KeyboardEvent) {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    void send();
  }
}

function onFile(event: Event) {
  const file = (event.target as HTMLInputElement).files?.[0];
  if (file) void onImport(file);
  if (importInput.value) importInput.value.value = "";
}
</script>

<template>
  <div class="lab manager">
    <header class="top">
      <div>
        <p class="eyebrow">Persona manager</p>
        <h1>Dino roster</h1>
        <a class="lab-link" href="#/">Back to the chat lab</a>
        <a class="lab-link" href="#/trace">Trace log</a>
      </div>
      <div class="status">
        <TraceToggle />
        <p>
          {{ username || "no name yet" }}
          · {{ mode === "local" ? "this browser only" : "shared hub" }}
          · memory core
        </p>
      </div>
    </header>

    <div v-if="needsName" class="gate">
      <form @submit.prevent="submitName">
        <p class="eyebrow">First visit</p>
        <h2>Pick a username</h2>
        <p class="hint">
          There is no password. This browser saves the name in local storage and a cookie.
          Lose the cookie and you pick a new name. Dinos saved under the old name stay there.
        </p>
        <p class="hint" v-if="mode === 'remote'">
          New names on the shared hub are capped at 100 per UTC day.
        </p>
        <label class="field">
          <span>Username</span>
          <input v-model="nameDraft" name="username" type="text" minlength="3" maxlength="20" autocomplete="username" required />
        </label>
        <p class="bad" v-if="nameError">{{ nameError }}</p>
        <button type="submit" :disabled="nameBusy">{{ nameBusy ? "Saving…" : "Use this name" }}</button>
      </form>
    </div>

    <div v-else class="columns">
      <section class="bench" data-pane="list" aria-label="Dino list">
        <h2>Dinos</h2>
        <label class="field">
          <span>Search</span>
          <input v-model="query" type="text" placeholder="Name or description" />
        </label>
        <label class="field">
          <span>Speech pattern</span>
          <select v-model="patternFilter">
            <option value="all">All patterns</option>
            <option v-for="pattern in SPEECH_PATTERNS" :key="pattern" :value="pattern">{{ pattern }}</option>
          </select>
        </label>
        <div class="row">
          <button type="button" @click="addDino">+ New</button>
          <button type="button" :disabled="!selected" @click="duplicate()">Duplicate</button>
        </div>
        <template v-for="group in (['bundled', 'mine', 'shared'] as const)" :key="group">
          <p class="group-label">{{ group === "mine" ? "My dinos" : group === "shared" ? "Shared dinos" : "Bundled" }}</p>
          <p class="hint" v-if="groups[group].length === 0">None in this filter.</p>
          <button
            v-for="row in groups[group]"
            :key="row.managed.persona.id"
            type="button"
            class="dino-card"
            :aria-current="selected?.managed.persona.id === row.managed.persona.id"
            @click="select(row.managed.persona.id)"
          >
            <i class="swatch" :style="{ background: row.managed.persona.color }"></i>
            <span>
              {{ row.managed.persona.name }}
              <small>
                {{ row.managed.profile.speechPattern }}
                <template v-if="row.dirty"> · unsaved</template>
                <template v-if="row.origin !== 'mine'"> · read-only</template>
              </small>
            </span>
          </button>
        </template>
      </section>

      <section class="editor" data-pane="editor" aria-label="Persona editor">
        <h2>Editor</h2>
        <p class="hint" v-if="!selected">Pick a dino.</p>
        <template v-else>
          <p class="hint" v-if="!editable">Read-only. Duplicate it if you want a copy you can edit.</p>
          <label class="field">
            <span>Name</span>
            <input
              :value="selected.managed.persona.name"
              type="text"
              maxlength="40"
              :disabled="!editable"
              @input="selected.managed.persona.name = ($event.target as HTMLInputElement).value; markDirty()"
            />
          </label>
          <label class="field">
            <span>Description</span>
            <textarea
              :value="selected.managed.persona.personality"
              :disabled="!editable"
              @input="selected.managed.persona.personality = ($event.target as HTMLTextAreaElement).value; markDirty()"
            ></textarea>
          </label>
          <label class="field">
            <span>Speech pattern</span>
            <select
              :value="selected.managed.profile.speechPattern"
              :disabled="!editable"
              @change="setPattern(($event.target as HTMLSelectElement).value)"
            >
              <option v-for="pattern in SPEECH_PATTERNS" :key="pattern" :value="pattern">{{ pattern }}</option>
            </select>
          </label>
          <label class="field">
            <span>Speech style</span>
            <input
              :value="selected.managed.persona.speechStyle"
              type="text"
              maxlength="256"
              :disabled="!editable"
              @input="setSpeechStyle(($event.target as HTMLInputElement).value)"
            />
          </label>

          <h2>Topics</h2>
          <p class="hint">Green is like, red is dislike, grey is neutral. One word works best in the test chat.</p>
          <div class="chips">
            <span v-for="topic in selected.managed.profile.topics" :key="topic.name" class="chip">
              <i class="dot" :class="topic.sentiment"></i>
              {{ topic.name }}
              <button v-if="editable" type="button" :aria-label="`Remove ${topic.name}`" @click="removeTopic(topic.name)">×</button>
            </span>
          </div>
          <div class="row" v-if="editable">
            <input v-model="topicName" type="text" maxlength="32" placeholder="topic name" aria-label="Topic name" />
            <select v-model="topicSentiment" aria-label="Topic sentiment">
              <option value="like">like</option>
              <option value="dislike">dislike</option>
              <option value="neutral">neutral</option>
            </select>
            <button type="button" @click="addTopicFromForm">Add topic</button>
          </div>

          <h2>Relationships</h2>
          <p class="hint">Friend, rival, or family. Affinity runs from -10 to 10.</p>
          <div v-for="rel in selected.managed.profile.relationships" :key="rel.subjectKey" class="row">
            <span class="grow">{{ rel.subjectKey }}</span>
            <select
              :value="rel.kind"
              :disabled="!editable"
              :aria-label="`Kind for ${rel.subjectKey}`"
              @change="setRelationshipKind(rel.subjectKey, ($event.target as HTMLSelectElement).value as 'friend' | 'rival' | 'family')"
            >
              <option value="friend">friend</option>
              <option value="rival">rival</option>
              <option value="family">family</option>
            </select>
            <input
              :value="rel.affinity"
              type="number"
              min="-10"
              max="10"
              :disabled="!editable"
              :aria-label="`Affinity for ${rel.subjectKey}`"
              @change="setAffinity(rel.subjectKey, Number(($event.target as HTMLInputElement).value))"
            />
            <button v-if="editable" type="button" class="slim" @click="removeRelationship(rel.subjectKey)">Remove</button>
          </div>
          <div class="row" v-if="editable">
            <input v-model="relSubject" type="text" maxlength="40" placeholder="Player or dino" aria-label="Relationship name" />
            <select v-model="relKind" aria-label="Relationship kind">
              <option value="friend">friend</option>
              <option value="rival">rival</option>
              <option value="family">family</option>
            </select>
            <button type="button" @click="addRelFromForm">Add relationship</button>
          </div>

          <h2>Minimum interest</h2>
          <p class="hint">1 to 10. The test chat replies above the comment minimum, likes at it, and ignores below it.</p>
          <div class="row">
            <label class="field" v-for="field in (['engagement', 'post', 'comment', 'like'] as const)" :key="field">
              <span>{{ field }}</span>
              <input
                :value="selected.managed.profile.interest[field]"
                type="number"
                min="1"
                max="10"
                :disabled="!editable"
                @change="setInterest(field, Number(($event.target as HTMLInputElement).value))"
              />
            </label>
          </div>

          <p class="hint">
            Owner {{ selected.owner || "—" }}
            · version {{ selected.version || "unsaved" }}
            <template v-if="selected.updatedAt"> · {{ selected.updatedAt }}</template>
          </p>
          <p class="bad" v-if="notice">{{ notice }}</p>
          <div class="row">
            <button v-if="editable" type="button" @click="save">Save</button>
            <button v-if="editable" type="button" @click="remove">Delete</button>
            <button type="button" @click="download">Export JSON</button>
            <button type="button" @click="importInput?.click()">Import JSON</button>
            <input ref="importInput" type="file" accept="application/json,.json" hidden @change="onFile" />
          </div>
        </template>
      </section>

      <section class="log" data-pane="chat" aria-label="Test chat">
        <div class="log-head">
          <h2>Test chat</h2>
          <p class="hint">Uses the draft, saved or not. ELIZA speaks. The log is the decision.</p>
          <label class="field">
            <span>Player name</span>
            <input v-model="playerName" type="text" maxlength="40" @change="refreshForPlayer" />
          </label>
        </div>
        <div class="sheet" role="log">
          <p class="hint" v-if="chatLines.length === 0">Say hello, or name a topic this dino likes.</p>
          <article v-for="(line, index) in chatLines" :key="index" class="bubble">
            <p class="who">{{ line.speaker }} · {{ line.role }}</p>
            <p>{{ line.text }}</p>
            <TraceRow v-if="tracesOn && line.traceId" :trace="traceById(line.traceId)" />
          </article>
        </div>
        <form class="composer" @submit.prevent="send">
          <textarea v-model="draft" placeholder="Say something" @keydown="onComposerKey"></textarea>
          <p class="bad" v-if="notice">{{ notice }}</p>
          <div class="row">
            <button type="submit" :disabled="chatBusy || !selected">{{ chatBusy ? "Talking…" : "Send" }}</button>
            <button type="button" @click="resetChat">Reset chat</button>
          </div>
        </form>
        <div class="tail">
          <h2>Decision log</h2>
          <p class="hint" v-if="logs.length === 0">No decisions yet.</p>
          <p v-for="row in logs" :key="row.id" class="log-row">
            <strong>{{ row.action }}</strong>
            · interest {{ row.metadata.interest }}
            · intent {{ row.metadata.intent }}
            · {{ row.metadata.quota }}
            <br />
            {{ row.metadata.reason }}
            <br />
            {{ row.metadata.memoryDelta }}
          </p>
          <h2>Memory</h2>
          <p class="hint">
            Rep {{ rep }} ({{ repLabel }}).
            Friendly at {{ repBands.friendlyAt }}, wary at {{ repBands.waryAt }}, hostile at {{ repBands.hostileAt }}.
            Lifetime earnings {{ earnings }}.
          </p>
          <p class="hint">Dino summary</p>
          <p class="memory-block">{{ dinoSummary || "Empty." }}</p>
          <p class="hint">With {{ playerName || username || "the player" }}</p>
          <p class="memory-block">{{ relationshipSummary || "Empty." }}</p>
          <button type="button" @click="resetMemory">Reset memory</button>
        </div>
      </section>
    </div>
  </div>
</template>
