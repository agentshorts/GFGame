import { createApp, type App as VueApp } from "vue";
import App from "./App.vue";
import PersonaManager from "./manager/PersonaManager.vue";
import TraceLog from "./trace/TraceLog.vue";
import HelpView from "./views/HelpView.vue";
import "./style.css";

function rootComponent() {
  if (location.hash.startsWith("#/personas")) return PersonaManager;
  if (location.hash.startsWith("#/trace")) return TraceLog;
  if (location.hash.startsWith("#/help")) return HelpView;
  return App;
}

let app: VueApp = createApp(rootComponent());
app.mount("#app");

window.addEventListener("hashchange", () => {
  app.unmount();
  app = createApp(rootComponent());
  app.mount("#app");
});
