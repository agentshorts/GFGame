<script setup lang="ts">
import { computed, ref } from "vue";
import { WIZARDS } from "../guide/copy";
import "../guide/guide.css";
import {
  dismissWizard,
  readWizardRecord,
  reopenWizard,
  setWizardStep,
  type WizardId,
} from "../guide/wizardState";

const props = defineProps<{ id: WizardId }>();

const wizard = computed(() => WIZARDS[props.id]);
const record = ref(readWizardRecord(props.id));
const step = computed(() => wizard.value.steps[record.value.step] ?? wizard.value.steps[0]);

function refresh(): void {
  record.value = readWizardRecord(props.id);
}

function skip(): void {
  dismissWizard(props.id);
  refresh();
}

function back(): void {
  setWizardStep(props.id, record.value.step - 1);
  refresh();
}

function next(): void {
  const last = wizard.value.steps.length - 1;
  if (record.value.step >= last) {
    dismissWizard(props.id);
  } else {
    setWizardStep(props.id, record.value.step + 1);
  }
  refresh();
}

function reopen(): void {
  reopenWizard(props.id);
  refresh();
}
</script>

<template>
  <button
    v-if="record.dismissed"
    type="button"
    class="guide-reopen"
    :data-reopen-wizard="id"
    @click="reopen"
  >
    {{ wizard.reopenLabel }}
  </button>
  <section
    v-else
    class="guide-wizard"
    :data-wizard="id"
    :aria-label="wizard.title"
  >
    <div class="guide-wizard-top">
      <p class="eyebrow">{{ wizard.eyebrow }} · {{ record.step + 1 }} / {{ wizard.steps.length }}</p>
      <button type="button" class="guide-skip" :aria-label="`Skip ${wizard.title}`" @click="skip">Skip</button>
    </div>
    <h3>{{ step?.title }}</h3>
    <p>{{ step?.body }}</p>
    <div class="guide-dots" aria-hidden="true">
      <i
        v-for="(item, index) in wizard.steps"
        :key="item.title"
        :class="{ on: index === record.step }"
      ></i>
    </div>
    <div class="guide-actions">
      <button type="button" :disabled="record.step === 0" @click="back">Back</button>
      <button type="button" class="guide-next" @click="next">
        {{ record.step >= wizard.steps.length - 1 ? "Done" : "Next" }}
      </button>
      <a class="guide-help-link" href="#/help">Help pictures</a>
    </div>
  </section>
</template>
