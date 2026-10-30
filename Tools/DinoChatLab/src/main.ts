import { createApp, type App as VueApp } from "vue";
import App from "./App.vue";
import PersonaManager from "./manager/PersonaManager.vue";
import "./style.css";

function rootComponent() {
  return location.hash.startsWith("#/personas") ? PersonaManager : App;
}

let app: VueApp = createApp(rootComponent());
app.mount("#app");

window.addEventListener("hashchange", () => {
  app.unmount();
  app = createApp(rootComponent());
  app.mount("#app");
});
