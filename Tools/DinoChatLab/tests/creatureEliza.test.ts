import { describe, expect, it } from "vitest";
import { CREATURE_EVENTS, CREATURE_MOODS, CREATURE_SPECIES } from "../src/data/creature/types";
import { loadCreatureBooks, speciesBook } from "../src/data/creature/load";
import { dinobookCreatureBark, familyLostBark } from "../src/dinobook/creaturePosts";
import {
  draftReply,
  matchPattern,
  moodPack,
  pickRule,
  reassemble,
  reflectWords,
  speakCreature,
  speciesForPersona,
  twistLine,
} from "../src/engines/creatureEliza";

const still = () => 0;

describe("creature rule selection", () => {
  it("loads a book for every species, mood, and dinobook event", () => {
    const books = loadCreatureBooks();
    expect(books.map((book) => book.species)).toEqual([...CREATURE_SPECIES]);
    for (const species of CREATURE_SPECIES) {
      const book = speciesBook(species);
      expect(book.schema).toBe("creature-rules/1");
      for (const mood of CREATURE_MOODS) {
        expect(book.moods[mood].rules.length).toBeGreaterThan(0);
        if (mood === "Angry") expect(book.moods[mood].growl).toBe("grrr");
        else expect(book.moods[mood].growl).toBe("");
      }
      for (const event of CREATURE_EVENTS) {
        expect(book.events[event].length).toBeGreaterThan(0);
      }
    }
  });

  it("ranks hands above berry, and angry hurt above hands", () => {
    const utterance = "look at my hands and a berry";
    const friendly = pickRule(moodPack("Nipper", "Friendly").rules, utterance);
    expect(friendly?.rule.keyword).toBe("hands");
    expect(friendly?.rule.rank).toBe(9);
    const hurtLine = "my hands hurt";
    expect(pickRule(moodPack("Rex", "Friendly").rules, hurtLine)?.rule.keyword).toBe("hands");
    expect(pickRule(moodPack("Rex", "Angry").rules, hurtLine)?.rule.keyword).toBe("hurt");
    expect(pickRule(moodPack("Hornback", "Grieving").rules, "my family and my hands")?.rule.keyword).toBe("family");
    expect(pickRule(moodPack("Sailback", "Bountied").rules, "the bounty and my hands")?.rule.keyword).toBe("bounty");
  });

  it("decomposes, reflects my to your, and reassembles", () => {
    expect(matchPattern("* my *", "look at my hands")).toEqual(["look at", "hands"]);
    expect(matchPattern("* my *", "my hands")).toEqual(["", "hands"]);
    expect(matchPattern("* berry", "nope")).toBeNull();
    expect(reflectWords("my hands")).toBe("your hands");
    expect(reflectWords("i am")).toBe("you are");
    expect(reassemble("why your (2) smell of berry?", ["look at", "hands"])).toBe("why your hands smell of berry?");
    expect(reassemble("why your $2 smell of berry?", ["", "hands"])).toBe("why your hands smell of berry?");
    const draft = draftReply({
      species: "Nipper",
      mood: "Friendly",
      utterance: "my hands",
      rng: still,
    });
    expect(draft.fellBack).toBe(false);
    expect(draft.keyword).toBe("hands");
    expect(draft.reflected).toBe(true);
    expect(draft.text).toBe("why your hands smell of berry?");
    const reflected = draftReply({
      species: "Raptor",
      mood: "Friendly",
      utterance: "see my berry pie",
      rng: still,
    });
    expect(reflected.keyword).toBe("berry");
    expect(reflected.reflected).toBe(true);
    expect(reflected.text).toBe("why your berry pie?");
  });

  it("picks a different rule set per mood and a different noise per species", () => {
    const friendly = speakCreature({ species: "Rex", mood: "Friendly", utterance: "my hands hurt", rng: still });
    const angry = speakCreature({ species: "Rex", mood: "Angry", utterance: "my hands hurt", rng: still });
    const wary = speakCreature({ species: "Rex", mood: "Wary", utterance: "my hands hurt", rng: still });
    expect(friendly.keyword).toBe("hands");
    expect(angry.keyword).toBe("hurt");
    expect(friendly.text).not.toBe(angry.text);
    expect(wary.text).not.toBe(friendly.text);
    expect(angry.growled).toBe(true);
    expect(angry.text.toLowerCase()).toContain("grrr");
    expect(friendly.text.toLowerCase()).not.toContain("grrr");
    expect(wary.text.toLowerCase()).not.toContain("grrr");
    const nipper = speakCreature({ species: "Nipper", mood: "Friendly", utterance: "look at my hands", rng: still });
    const rex = speakCreature({ species: "Rex", mood: "Friendly", utterance: "look at my hands", rng: still });
    expect(nipper.text).toContain("mip mip");
    expect(rex.text).toContain("hrrk");
    expect(nipper.text).not.toBe(rex.text);
  });

  it("growls when angry, falls back when nothing matches, and twists a word", () => {
    const angry = speakCreature({ species: "Raptor", mood: "Angry", utterance: "hello there", rng: still });
    expect(angry.text.toLowerCase()).toContain("grrr");
    expect(angry.keyword).toBe("hello");
    const missed = speakCreature({ species: "Hornback", mood: "Grieving", utterance: "quantum xylophone", rng: still });
    expect(missed.fellBack).toBe(true);
    expect(missed.keyword).toBeNull();
    expect(missed.text.toLowerCase()).toContain("hornback");
    expect(missed.text.toLowerCase()).not.toContain("grrr");
    const twisted = twistLine("why your hands", { noises: ["mip mip"], growl: "", questions: [] }, still);
    expect(twisted.toLowerCase()).toContain("whyy");
    expect(twisted).toContain("mip mip");
  });

  it("fills dinobook event templates, including the angry growl", () => {
    const lost = speakCreature({
      species: "Sailback",
      mood: "Angry",
      event: "family_lost",
      player: "Pip",
      rng: still,
    });
    expect(lost.event).toBe("family_lost");
    expect(lost.fellBack).toBe(false);
    expect(lost.text.toLowerCase()).toMatch(/gone|famlee|family/);
    expect(lost.text.toLowerCase()).toContain("grrr");
    expect(lost.text).toContain("Pip");
    for (const event of CREATURE_EVENTS) {
      const line = speakCreature({ species: "Nipper", mood: "Friendly", event, player: "Pip", rng: still });
      expect(line.text.length).toBeGreaterThan(0);
      expect(line.event).toBe(event);
    }
    const hurt = dinobookCreatureBark("attacked", "scythepaw", "Pip");
    expect(hurt?.toLowerCase()).toContain("grrr");
    expect(hurt?.toLowerCase()).toMatch(/hurt|hurrt/);
    expect(dinobookCreatureBark("payday", "rex", "Pip")).toBeNull();
    expect(familyLostBark("big-grumble", "Pip").toLowerCase()).toMatch(/gone|famlee|family/);
    expect(speciesForPersona("big-grumble")).toBe("Rex");
    expect(speciesForPersona("ripwing")).toBe("Sailback");
    expect(speciesForPersona("tusksaw")).toBe("Hornback");
    expect(speciesForPersona("chatty-bird")).toBe("Nipper");
    expect(speciesForPersona("stranger")).toBe("Nipper");
  });
});
