import { describe, expect, it } from "vitest";
import { DINOBOOK_EVENTS } from "../src/dinobook/types";
import { PANELS, PANEL_IDS, WIZARDS } from "../src/guide/copy";
import {
  PANEL_INFO_KEY,
  WIZARD_IDS,
  WIZARD_STEP_COUNTS,
  WIZARD_STORAGE_KEY,
  dismissWizard,
  emptyWizardStore,
  readPanelOpen,
  readWizardStore,
  reopenWizard,
  setWizardStep,
  skipWizard,
  wizardStepCount,
  writePanelOpen,
  type GuideStorage,
} from "../src/guide/wizardState";

function memory(seed?: Record<string, string>): GuideStorage & { snapshot(): Record<string, string> } {
  const map = new Map(Object.entries(seed ?? {}));
  return {
    getItem(key) {
      return map.get(key) ?? null;
    },
    setItem(key, value) {
      map.set(key, value);
    },
    snapshot() {
      return Object.fromEntries(map);
    },
  };
}

describe("wizard state", () => {
  it("starts with every guide open on step 0", () => {
    const store = readWizardStore(memory());
    expect(store).toEqual(emptyWizardStore());
    for (const id of WIZARD_IDS) {
      expect(store[id]).toEqual({ dismissed: false, step: 0 });
    }
  });

  it("keeps the dismissal in storage and leaves the other guides alone", () => {
    const storage = memory();
    skipWizard("crew", storage);
    setWizardStep("traces", 2, storage);
    const again = readWizardStore(storage);
    expect(again.crew).toEqual({ dismissed: true, step: 0 });
    expect(again.events).toEqual({ dismissed: false, step: 0 });
    expect(again.traces).toEqual({ dismissed: false, step: 2 });
    expect(WIZARD_STORAGE_KEY).toBe("dcl:wizards");
    const saved = JSON.parse(storage.snapshot()[WIZARD_STORAGE_KEY] ?? "{}") as { crew: { dismissed: boolean } };
    expect(saved.crew.dismissed).toBe(true);
  });

  it("treats skip and dismiss as the same remembered flag", () => {
    const skipped = memory();
    const dismissed = memory();
    skipWizard("events", skipped);
    dismissWizard("events", dismissed);
    expect(readWizardStore(skipped).events.dismissed).toBe(true);
    expect(readWizardStore(dismissed).events).toEqual(readWizardStore(skipped).events);
  });

  it("remembers the step until the guide is dismissed", () => {
    const storage = memory();
    setWizardStep("traces", 3, storage);
    expect(readWizardStore(storage).traces).toEqual({ dismissed: false, step: 3 });
    dismissWizard("traces", storage);
    expect(readWizardStore(storage).traces).toEqual({ dismissed: true, step: 3 });
  });

  it("clamps a step that walks off either end", () => {
    const storage = memory();
    setWizardStep("crew", 40, storage);
    expect(readWizardStore(storage).crew.step).toBe(wizardStepCount("crew") - 1);
    setWizardStep("crew", -3, storage);
    expect(readWizardStore(storage).crew.step).toBe(0);
  });

  it("reopens a dismissed guide at the first step", () => {
    const storage = memory();
    setWizardStep("events", 5, storage);
    skipWizard("events", storage);
    reopenWizard("events", storage);
    expect(readWizardStore(storage).events).toEqual({ dismissed: false, step: 0 });
    expect(readWizardStore(storage).crew.dismissed).toBe(false);
  });

  it("ignores corrupt storage and a dismissal that is not strictly true", () => {
    expect(readWizardStore(memory({ [WIZARD_STORAGE_KEY]: "{not json" })).crew.dismissed).toBe(false);
    expect(readWizardStore(memory({ [WIZARD_STORAGE_KEY]: "null" })).events.step).toBe(0);
    expect(readWizardStore(memory({ [WIZARD_STORAGE_KEY]: "[]" })).traces.dismissed).toBe(false);
    const sloppy = memory({
      [WIZARD_STORAGE_KEY]: JSON.stringify({
        crew: { dismissed: "yes", step: "2" },
        events: { step: 1.9 },
        extra: { dismissed: true },
      }),
    });
    const store = readWizardStore(sloppy);
    expect(store.crew).toEqual({ dismissed: false, step: 0 });
    expect(store.events).toEqual({ dismissed: false, step: 1 });
    expect(store.traces).toEqual({ dismissed: false, step: 0 });
  });

  it("survives a storage implementation that throws", () => {
    const broken: GuideStorage = {
      getItem() {
        throw new Error("blocked");
      },
      setItem() {
        throw new Error("blocked");
      },
    };
    expect(readWizardStore(broken).crew).toEqual({ dismissed: false, step: 0 });
    expect(() => skipWizard("crew", broken)).not.toThrow();
    expect(() => reopenWizard("traces", broken)).not.toThrow();
  });

  it("covers crew, every Dinobook event, and the trace readout", () => {
    for (const id of WIZARD_IDS) {
      expect(WIZARDS[id].steps.length).toBe(WIZARD_STEP_COUNTS[id]);
      expect(WIZARDS[id].steps.length).toBe(wizardStepCount(id));
      expect(WIZARDS[id].reopenLabel.length).toBeGreaterThan(0);
    }
    const crew = WIZARDS.crew.steps.map((step) => `${step.title} ${step.body}`).join(" ").toLowerCase();
    expect(crew).toContain("persona");
    expect(crew).toContain("wage");
    expect(crew).toContain("job");
    expect(WIZARDS.events.steps.flatMap((step) => (step.eventId ? [step.eventId] : []))).toEqual(
      DINOBOOK_EVENTS.map((event) => event.id),
    );
    for (const step of WIZARDS.events.steps) {
      expect(step.body.length).toBeGreaterThan(40);
    }
    const traces = WIZARDS.traces.steps.map((step) => `${step.title} ${step.body}`).join(" ").toLowerCase();
    expect(traces).toContain("eliza");
    expect(traces).toContain("badge");
    expect(traces).toContain("reason");
    expect(traces).toContain("#/trace");
    expect(traces).toContain("filter");
  });
});

describe("panel info cards", () => {
  it("starts collapsed and remembers one panel without opening the others", () => {
    const storage = memory();
    expect(readPanelOpen("talk", storage)).toBe(false);
    writePanelOpen("talk", true, storage);
    writePanelOpen("decision", true, storage);
    writePanelOpen("talk", false, storage);
    expect(readPanelOpen("talk", storage)).toBe(false);
    expect(readPanelOpen("decision", storage)).toBe(true);
    expect(PANEL_INFO_KEY).toBe("dcl:panel-info");
    const saved = JSON.parse(storage.snapshot()[PANEL_INFO_KEY] ?? "{}") as Record<string, boolean>;
    expect(saved.talk).toBeUndefined();
    expect(saved.decision).toBe(true);
  });

  it("ignores corrupt panel storage", () => {
    const storage = memory({ [PANEL_INFO_KEY]: "{nope" });
    expect(readPanelOpen("talk", storage)).toBe(false);
    expect(() => writePanelOpen("talk", true, {
      getItem() {
        throw new Error("blocked");
      },
      setItem() {
        throw new Error("blocked");
      },
    })).not.toThrow();
  });

  it("gives every panel a purpose, a first try, and a control list", () => {
    expect(PANEL_IDS.length).toBeGreaterThanOrEqual(8);
    for (const id of PANEL_IDS) {
      const panel = PANELS[id];
      expect(panel?.purpose.length).toBeGreaterThan(20);
      expect(panel?.tryFirst.length).toBeGreaterThan(10);
      expect(panel?.controls.length).toBeGreaterThan(0);
      for (const control of panel?.controls ?? []) {
        expect(control.name.length).toBeGreaterThan(0);
        expect(control.meaning.length).toBeGreaterThan(8);
      }
    }
  });
});
