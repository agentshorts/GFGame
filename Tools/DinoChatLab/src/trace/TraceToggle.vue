<script setup lang="ts">
import { useTrace } from "./useTrace";

const { enabled } = useTrace();
</script>

<template>
  <label class="check trace-toggle">
    <input v-model="enabled" type="checkbox" />
    <span>Trace</span>
  </label>
</template>
