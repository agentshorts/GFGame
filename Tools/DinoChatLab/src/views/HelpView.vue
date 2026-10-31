<script setup lang="ts">
import shots from "../guide/helpShots.json";
import "../guide/guide.css";

function src(file: string): string {
  return `${import.meta.env.BASE_URL}help/${file}`;
}
</script>

<template>
  <div class="lab">
    <header class="top">
      <div>
        <p class="eyebrow">Picture index</p>
        <h1>Help</h1>
        <a class="lab-link" href="#/">Chat lab</a>
        <a class="lab-link" href="#/personas">Personas</a>
        <a class="lab-link" href="#/trace">Trace log</a>
      </div>
    </header>
    <article class="help-sheet">
      <h2>Screens, panels, and the three guides</h2>
      <p class="help-lead">
        Crew setup, Dinobook events, and reading traces each open on the panel they explain.
        Skip stores a dismissal in this browser. The same guide opens again from its button.
        Every panel also has an About card: what it is for, what to try first, and what the controls mean.
      </p>
      <figure v-for="shot in shots" :key="shot.file">
        <img :src="src(shot.file)" :alt="`${shot.title}. ${shot.caption}`" />
        <figcaption>
          <strong>{{ shot.title }}</strong>
          {{ shot.caption }}
        </figcaption>
      </figure>
    </article>
  </div>
</template>
