<script setup lang="ts">
import { computed, ref } from "vue";
import { PANELS } from "../guide/copy";
import "../guide/guide.css";
import { readPanelOpen, writePanelOpen } from "../guide/wizardState";

const props = defineProps<{ id: string }>();

const panel = computed(() => PANELS[props.id] ?? null);
const open = ref(panel.value ? readPanelOpen(props.id) : false);

function toggle(): void {
  open.value = !open.value;
  writePanelOpen(props.id, open.value);
}
</script>

<template>
  <details
    v-if="panel"
    class="panel-info"
    :data-panel-info="id"
    :open="open ? true : undefined"
  >
    <summary @click.prevent="toggle">
      <span class="panel-info-kicker">About this panel</span>
      {{ panel.title }}
    </summary>
    <p>{{ panel.purpose }}</p>
    <p><strong>Try first.</strong> {{ panel.tryFirst }}</p>
    <dl>
      <template v-for="control in panel.controls" :key="control.name">
        <dt>{{ control.name }}</dt>
        <dd>{{ control.meaning }}</dd>
      </template>
    </dl>
  </details>
</template>
