import { describe, expect, it, vi } from "vitest";
import grumbleJson from "../personas/01-big-grumble.json";
import tusksawJson from "../personas/02-tusksaw.json";
import ripwingJson from "../personas/03-ripwing.json";
import scythepawJson from "../personas/04-scythepaw.json";
import lanternJson from "../personas/05-lanternmaw.json";
import birdJson from "../personas/06-chatty-bird.json";
import { mateRank } from "../src/core/stats";
import { getWork, LocalStorageAdapter, memoryKeyValueStore } from "../src/core/storage";
import { BEG_MANGO, EXTORT_LINE } from "../src/logic/alignment";
import { estimateTokens } from "../src/logic/context";
import { parsePersona } from "../src/logic/persona";
import {
  acceptSpeech,
  applyEventDelta,
  assignRoles,
  briefFromCoreStats,
  chooseApproach,
  clampToTokens,
  createLocalCore,
  creditPayday,
  finishBrief,
  finishWage,
  LAB_TREE,
  loadCorePort,
  normalizeLine,
  NOTE_TOKEN_CAP,
  pickLeader,
  portFromCore,
  POST_TOKEN_CAP,
  REP_BAND_THRESHOLDS,
  repBand,
  runDinobookEvent,
  seedBrief,
  withReference,
  type FamilyBrief,
  type RosterDino,
} from "../src/dinobook";

const roster: RosterDino[] = [grumbleJson, tusksawJson, ripwingJson, scythepawJson, lanternJson, birdJson]
  .map(parsePersona)
  .map((persona) => ({
    id: persona.id,
    name: persona.name,
    alignment: persona.alignment,
    color: persona.color,
  }));

const at = "2026-10-30T00:00:00.000Z";

function briefs(partials: Partial<FamilyBrief>[] = []): FamilyBrief[] {
  const seeded = LAB_TREE.families.map((family) => seedBrief(family.familyId));
  return seeded.map((brief) => {
    const patch = partials.find((row) => row.familyId === brief.familyId);
    return patch ? finishBrief({ ...brief, ...patch, familyId: brief.familyId }) : brief;
  });
}

function run(event: Parameters<typeof runDinobookEvent>[0]["event"], extra: Partial<Parameters<typeof runDinobookEvent>[0]> = {}) {
  return runDinobookEvent({
    event,
    playerName: "Pip",
    roster,
    briefs: briefs(),
    at,
    ...extra,
  });
}

function namesIn(text: string): string[] {
  return roster.map((dino) => dino.name).filter((name) => text.toLowerCase().includes(name.toLowerCase()));
}

describe("dinobook tree", () => {
  it("runs from elders to families, with friends as side links", () => {
    expect(LAB_TREE.families.map((family) => family.elderId)).toEqual(["lanternmaw", "scythepaw", "chatty-bird"]);
    for (const family of LAB_TREE.families) {
      expect(family.memberIds[0]).toBe(family.elderId);
    }
    for (const link of LAB_TREE.sideLinks) {
      const homeA = LAB_TREE.families.find((family) => family.memberIds.includes(link.a));
      const homeB = LAB_TREE.families.find((family) => family.memberIds.includes(link.b));
      expect(homeA?.familyId).not.toBe(homeB?.familyId);
    }
  });
});

describe("dinobook posts", () => {
  it("keeps every post and all-chat line inside 80 tokens", () => {
    const events = ["player-joined", "attacked", "refused-mango", "rental-downed", "payday", "summon-big-grumble"] as const;
    for (const event of events) {
      const result = run(event, { policy: { maxSpeakersPerEvent: 6 } });
      for (const post of result.posts) {
        expect(estimateTokens(post.content)).toBeLessThanOrEqual(POST_TOKEN_CAP);
        expect(post.tokens).toBe(estimateTokens(post.content));
      }
      for (const line of result.allChat) {
        expect(estimateTokens(line.text)).toBeLessThanOrEqual(POST_TOKEN_CAP);
      }
    }
    const long = clampToTokens("mango ".repeat(200), POST_TOKEN_CAP);
    expect(estimateTokens(long)).toBeLessThanOrEqual(POST_TOKEN_CAP);
  });

  it("threads family replies under the elder and friends as side links", () => {
    const attacked = run("attacked", { policy: { maxSpeakersPerEvent: 5 } });
    const elder = attacked.posts.find((post) => post.authorId === "lanternmaw");
    const member = attacked.posts.find((post) => post.authorId === "tusksaw");
    const otherElder = attacked.posts.find((post) => post.authorId === "scythepaw");
    const screen = attacked.posts.find((post) => post.authorId === "ripwing");
    const friend = attacked.posts.find((post) => post.authorId === "chatty-bird");
    expect(elder?.link).toBe("root");
    expect(member?.link).toBe("child");
    expect(member?.parentId).toBe(elder?.id);
    expect(member?.familyId).toBe("swamp");
    expect(otherElder?.link).toBe("root");
    expect(screen?.link).toBe("child");
    expect(screen?.parentId).toBe(otherElder?.id);
    expect(screen?.familyId).toBe("sickle");
    expect(friend?.link).toBe("side");
    expect(friend?.parentId).toBe(otherElder?.id);
    expect(attacked.posts.findIndex((post) => post.authorId === "tusksaw"))
      .toBeGreaterThan(attacked.posts.findIndex((post) => post.authorId === "lanternmaw"));
  });
});

describe("coordination", () => {
  it("has the leader post a toll, then block, take a hit, and demand mango in all-chat", () => {
    const result = run("attacked");
    expect(result.leaderId).toBe("lanternmaw");
    expect(result.planId).toBe("toll");
    expect(result.posts.map((post) => post.role).slice(0, 3)).toEqual(["lead", "block-path", "take-hit"]);
    expect(result.posts[0]?.content).toContain("blocks the path");
    expect(result.posts[0]?.content).toContain("takes the hit");
    expect(result.posts[0]?.content.toLowerCase()).toContain("mango");
    expect(result.allChat).toHaveLength(1);
    expect(result.allChat[0]?.speakerId).toBe("lanternmaw");
    expect(result.allChat[0]?.text).toBe(EXTORT_LINE);
    expect(result.allChat[0]?.reason).toContain("leader");
  });

  it("changes the plan with personality and family", () => {
    const kind = run("attacked", {
      briefs: briefs([
        { familyId: "swamp", rep: 40, kills: 0 },
        { familyId: "sickle", rep: -60, kills: 0 },
      ]),
    });
    expect(kind.leaderId).toBe("scythepaw");
    expect(kind.planId).toBe("cover");
    expect(kind.posts[0]?.content).toContain("Sickle");
    expect(kind.posts[0]?.content).toContain("ask for mango");
    expect(kind.allChat[0]?.text).toBe(BEG_MANGO);

    const harsh = run("attacked");
    expect(harsh.posts[0]?.content).toContain("Swamp");
    expect(harsh.posts[0]?.content).toContain("demand mango");
    expect(harsh.posts[0]?.content).not.toBe(kind.posts[0]?.content);

    const welcome = run("player-joined");
    expect(welcome.planId).toBe("welcome");
    expect(welcome.leaderId).toBe("chatty-bird");
    expect(welcome.posts.some((post) => post.role === "block-path")).toBe(false);
    expect(welcome.posts.some((post) => post.link === "side")).toBe(true);
    expect(welcome.allChat[0]?.text).toContain("Pip");

    const rental = run("rental-downed");
    expect(rental.planId).toBe("repossess");
    expect(rental.posts[0]?.content).toContain("demand it back");
    expect(rental.allChat[0]?.text).toContain("rental");
  });

  it("picks the leader from the record", () => {
    const seeded = briefs();
    expect(pickLeader("attacked", LAB_TREE, roster, seeded)).toBe("lanternmaw");
    expect(pickLeader("player-joined", LAB_TREE, roster, seeded)).toBe("chatty-bird");
    expect(pickLeader("refused-mango", LAB_TREE, roster, seeded)).toBe("lanternmaw");
  });
});

describe("Big Grumble", () => {
  it("polls every elder and chases when a hostile family has kills", () => {
    const result = run("summon-big-grumble");
    expect(result.approach).toBe("chase");
    expect(result.approachReason).toContain("hostile");
    const reports = result.posts.filter((post) => post.role === "report");
    expect(reports.map((post) => post.authorId)).toEqual(["lanternmaw", "scythepaw", "chatty-bird"]);
    for (const report of reports) {
      expect(report.link).toBe("child");
      expect(report.content).toContain("rep ");
      expect(report.content).toContain("kills");
      expect(report.content).toContain("bounties");
      expect(report.content).toContain("rentals");
      expect(report.parentId).toBe(result.posts[0]?.id);
    }
    expect(result.posts[0]?.authorId).toBe("big-grumble");
    const verdict = result.posts.find((post) => post.role === "verdict");
    expect(verdict?.content).toContain("Lanternmaw");
    expect(verdict?.content).toContain("Scythepaw");
    expect(verdict?.content).toContain("Chatty Bird");
    expect(result.allChat).toHaveLength(1);
    expect(result.allChat[0]?.speakerId).toBe("big-grumble");
    expect(result.allChat[0]?.text).toBe("Why you running, huh? Why you running?");
  });

  it("changes approach from the elder replies", () => {
    const play = run("summon-big-grumble", {
      briefs: [
        finishBrief({ familyId: "swamp", rep: 30 }),
        finishBrief({ familyId: "sickle", rep: 4 }),
        finishBrief({ familyId: "roost", rep: 40, mangoGiven: 1 }),
      ],
    });
    expect(play.approach).toBe("play");
    expect(play.allChat[0]?.text).toContain("just want to play");

    const bounty = run("summon-big-grumble", {
      briefs: [
        finishBrief({ familyId: "swamp", rep: 0, bountiesOnPlayer: 2 }),
        finishBrief({ familyId: "sickle", rep: 0 }),
        finishBrief({ familyId: "roost", rep: 10 }),
      ],
    });
    expect(bounty.approach).toBe("collect-bounty");

    const rental = run("summon-big-grumble", {
      briefs: [
        finishBrief({ familyId: "swamp", rep: 0, rentalsDowned: 1 }),
        finishBrief({ familyId: "sickle", rep: 0 }),
        finishBrief({ familyId: "roost", rep: 0 }),
      ],
    });
    expect(rental.approach).toBe("return-rentals");

    const mango = run("summon-big-grumble", {
      briefs: [
        finishBrief({ familyId: "swamp", rep: 0, mangoRefused: 3, mangoGiven: 1 }),
        finishBrief({ familyId: "sickle", rep: 0 }),
        finishBrief({ familyId: "roost", rep: 0 }),
      ],
    });
    expect(mango.approach).toBe("demand-mango");

    const watch = run("summon-big-grumble", {
      briefs: [
        finishBrief({ familyId: "swamp", rep: -50, kills: 0 }),
        finishBrief({ familyId: "sickle", rep: 0 }),
        finishBrief({ familyId: "roost", rep: 0 }),
      ],
    });
    expect(watch.approach).toBe("watch");

    const elderKill = chooseApproach([finishBrief({ familyId: "swamp", rep: 10, elderKills: 1 })]);
    expect(elderKill.id).toBe("chase");
    const chaseBeatsBounty = chooseApproach([
      finishBrief({ familyId: "swamp", rep: -50, kills: 2, bountiesOnPlayer: 4 }),
    ]);
    expect(chaseBeatsBounty.id).toBe("chase");
  });
});

describe("trace and credit", () => {
  it("caps elder notes instead of restacking earlier posts", () => {
    let memory: Record<string, string> = {};
    let prior: string[] = [];
    let idStart = 0;
    const leads: string[] = [];
    for (let step = 0; step < 4; step += 1) {
      const result = run("attacked", { memory, priorTexts: prior, idStart });
      const lead = result.posts.find((post) => post.role === "lead");
      if (step === 0) {
        expect(lead?.authorId).toBe("lanternmaw");
        expect(lead?.content.toLowerCase()).not.toContain("still noted");
      } else if (step === 1) {
        expect(lead?.authorId).toBe("lanternmaw");
        expect(lead?.content.match(/still noted/gi)).toHaveLength(1);
        expect(lead?.content.endsWith("…")).toBe(false);
        expect(lead!.tokens).toBeLessThan(POST_TOKEN_CAP);
      } else {
        expect(lead).toBeUndefined();
        expect(result.skipped.some((skip) => skip.dinoId === "lanternmaw" && skip.reason === "duplicate")).toBe(true);
      }
      if (lead) leads.push(lead.content);
      const stored = result.memory.lanternmaw ?? "";
      expect(stored.toLowerCase()).not.toContain("still noted");
      expect(estimateTokens(stored)).toBeLessThanOrEqual(NOTE_TOKEN_CAP);
      memory = result.memory;
      prior = [
        ...prior,
        ...result.posts.map((post) => post.content),
        ...result.allChat.map((line) => line.text),
      ];
      idStart = result.nextId;
    }
    expect(leads).toHaveLength(2);
    expect(estimateTokens(leads[1]!) - estimateTokens(leads[0]!)).toBeLessThanOrEqual(NOTE_TOKEN_CAP + 4);
    expect(leads[1]!.split(/still noted:/i)).toHaveLength(2);
  });

  it("credits the welcome author, not the dino who spoke in between", () => {
    const welcome = run("player-joined", { policy: { maxSpeakersPerEvent: 6 } });
    expect(welcome.posts.find((post) => post.role === "lead")?.authorId).toBe("chatty-bird");
    const ripwing = welcome.posts.find((post) => post.authorId === "ripwing");
    expect(ripwing?.content).toContain("Chatty Bird said it");
    expect(ripwing?.content).not.toContain("Lanternmaw said it");
    const trace = welcome.traces.find((row) => row.dinoId === "ripwing");
    expect(trace?.attribution?.name).toBe("Chatty Bird");
    expect(trace?.attribution?.because).toBe("parent");
    expect(trace?.eventLabel).toBe("Player joined");
    expect(welcome.posts.every((post) => post.runId === welcome.runId)).toBe(true);
  });

  it("credits Big Grumble on every elder report", () => {
    const summon = run("summon-big-grumble", { policy: { maxSpeakersPerEvent: 6 } });
    const reports = summon.posts.filter((post) => post.role === "report");
    expect(reports.length).toBeGreaterThan(1);
    for (const post of reports) expect(post.content).toContain("Big Grumble asked");
    expect(summon.traces.some((row) => row.kind === "elder" && row.chosenSource === "alignment")).toBe(true);
  });

  it("credits the payday brag for a later find", () => {
    const pay = run("payday", { policy: { maxSpeakersPerEvent: 6 } });
    const brag = pay.posts.find((post) => post.role === "brag-pay");
    const mineral = pay.posts.find((post) => post.role === "mineral-find");
    expect(brag?.authorName).toBeTruthy();
    expect(mineral?.content).toContain(`${brag?.authorName} talked pay`);
    expect(pay.traces.some((row) => row.kind === "payday")).toBe(true);
  });
});

describe("anti-spam", () => {
  it("drops duplicate lines, caps speakers, and makes later lines name someone", () => {
    const first = acceptSpeech("Burrowed. I block the path.", [], null);
    const second = acceptSpeech("Burrowed. I block the path.", [first!.text], "Lanternmaw");
    expect(second).not.toBeNull();
    expect(normalizeLine(second!.text)).not.toBe(normalizeLine(first!.text));
    expect(second!.text).toContain("Lanternmaw");
    expect(acceptSpeech("Burrowed. I block the path.", [first!.text], null)).toBeNull();

    const capped = run("attacked", { policy: { maxSpeakersPerEvent: 2 } });
    expect(new Set(capped.posts.map((post) => post.authorId)).size).toBeLessThanOrEqual(2);
    expect(capped.skipped.some((skip) => skip.reason === "speaker-cap")).toBe(true);
    expect(capped.decisions.some((row) => row.action === "NONE")).toBe(true);

    const full = run("attacked", { policy: { maxSpeakersPerEvent: 5 } });
    expect(new Set(full.posts.map((post) => normalizeLine(post.content))).size).toBe(full.posts.length);
    for (const [index, post] of full.posts.entries()) {
      if (index === 0) continue;
      const earlier = full.posts.slice(0, index).map((row) => row.authorName);
      expect(earlier.some((name) => post.content.includes(name))).toBe(true);
    }
    const speakers = full.posts.map((post) => post.authorId);
    expect(new Set(speakers).size).toBe(speakers.length);
  });

  it("keeps all-chat to the budget, usually one leader line", () => {
    expect(run("attacked", { chat: { maxLines: 0 } }).allChat).toEqual([]);
    expect(run("attacked", { chat: { maxTokens: 0 } }).allChat).toEqual([]);
    const shared = run("attacked", { chat: { leaderOnly: false, maxLines: 3, maxTokens: 80 } });
    expect(shared.allChat.length).toBeGreaterThan(1);
    expect(shared.allChat[0]?.speakerId).toBe(shared.leaderId);
    expect(shared.allChat[1]?.text).toContain(shared.allChat[0]?.speakerName ?? "");
    const spent = shared.allChat.reduce((sum, line) => sum + line.tokens, 0);
    expect(spent).toBeLessThanOrEqual(80);
  });

  it("does not repeat a line already on the feed", () => {
    const first = run("attacked");
    const second = run("attacked", {
      memory: first.memory,
      priorTexts: [...first.posts.map((post) => post.content), ...first.allChat.map((line) => line.text)],
    });
    const seen = new Set(first.posts.map((post) => normalizeLine(post.content)));
    for (const post of second.posts) {
      expect(seen.has(normalizeLine(post.content))).toBe(false);
    }
    expect(second.posts[0]?.content).toContain("Still noted");
    expect(second.skipped.some((skip) => skip.reason === "duplicate")).toBe(true);
  });

  it("filters a post the same way chat does", () => {
    const cleaned = acceptSpeech("that is shit on the path", [], null);
    expect(cleaned?.filtered).toBe(true);
    expect(cleaned?.text.toLowerCase()).not.toContain("shit");
  });

  it("keeps a reference inside the token cap", () => {
    const line = withReference("mango ".repeat(80), "Lanternmaw", 80);
    expect(estimateTokens(line)).toBeLessThanOrEqual(80);
    expect(line).toContain("Lanternmaw");
  });
});

describe("core port", () => {
  it("seeds a local record and applies one family's event", () => {
    const core = createLocalCore();
    const before = core.briefs("Pip");
    expect(before.find((row) => row.familyId === "swamp")?.band).toBe("hostile");
    core.apply("Pip", "swamp", "refused-mango");
    const swamp = core.briefs("Pip").find((row) => row.familyId === "swamp");
    expect(swamp?.mangoRefused).toBe(2);
    expect(core.briefs("Pip").find((row) => row.familyId === "roost")?.mangoRefused).toBe(0);
    const down = applyEventDelta(finishBrief({ familyId: "sickle", rep: -100 }), "attacked");
    expect(down.rep).toBe(-100);
    expect(down.kills).toBe(1);
    expect(down.band).toBe("hostile");
  });

  it("maps a snake_case player_stats row and a core module bag", () => {
    const brief = briefFromCoreStats({
      family_id: "swamp",
      rep: -12,
      kills: 2,
      elder_kills: 1,
      bounties_on_player: 3,
      bounties_claimed: 1,
      rentals_downed: 4,
      mango_refused: 5,
      hits_dealt: 6,
    });
    expect(brief).toMatchObject({
      familyId: "swamp",
      rep: -12,
      band: "wary",
      kills: 2,
      elderKills: 1,
      bountiesOnPlayer: 3,
      bountiesClaimed: 1,
      rentalsDowned: 4,
      mangoRefused: 5,
      hitsDealt: 6,
    });

    let stored: unknown = null;
    const port = portFromCore({
      storage: {
        getStats: () => [{ family_id: "sickle", rep: 30, kills: 0 }],
      },
      memory: {
        getMemory: () => ({ summary: "claws folded" }),
        putMemory: (memory: unknown) => {
          stored = memory;
        },
      },
    });
    expect(port?.source).toBe("src/core");
    const rows = port?.briefs("Pip") ?? [];
    expect(rows.map((row) => row.familyId)).toEqual(["sickle"]);
    expect(rows.find((row) => row.familyId === "sickle")).toMatchObject({ rep: 30, band: "friendly" });
    expect(port?.recall("scythepaw")).toBe("claws folded");
    port?.remember("scythepaw", "Petit Cake.");
    expect(stored).toMatchObject({
      dinoId: "scythepaw",
      summary: "Petit Cake.",
      turnsSinceSummary: 0,
    });
    expect(portFromCore({})).toBeNull();
  });

  it("binds src/core instead of staying local when the core is in the tree", async () => {
    const port = await loadCorePort();
    expect(port.source).toBe("src/core");
    expect(port.briefs("Pip")).toEqual([]);
    expect(REP_BAND_THRESHOLDS).toEqual({ friendlyAt: 20, waryAt: -8, hostileAt: -40 });
    expect(repBand(24)).toBe("friendly");
    expect(repBand(19)).toBe("neutral");
    expect(repBand(-7)).toBe("neutral");
    expect(repBand(-9)).toBe("wary");
    expect(repBand(-40)).toBe("hostile");
    port.setWage("Pip", finishWage({
      dinoId: "scythepaw",
      factoryId: "bone-works",
      factoryName: "Bone works",
      shiftPay: 500,
      lifetimeEarnings: 500,
      hiredAt: "2026-01-01T00:00:00.000Z",
      mineralsFound: 0,
      mateRank: 0,
    }));
    expect(getWork("scythepaw")?.lifetimeEarnings).toBe(500);
    const crew = port.wages("Pip");
    expect(crew.map((card) => card.dinoId)).toEqual(["scythepaw"]);
    expect(crew.find((card) => card.dinoId === "scythepaw")?.mateRank).toBe(mateRank(crew, "scythepaw"));
  });

  it("credits each shift and does not mint a missing seed total", () => {
    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    adapter.putWork({
      dinoId: "tusksaw",
      employerId: "fossil-pit",
      employment: "tamed",
      lifetimeEarnings: 100,
      mineralsFound: 0,
      seekingJob: false,
      hiredAt: "2026-01-01T00:00:00.000Z",
    });
    const putWork = vi.fn();
    const port = portFromCore({
      storage: {
        getStats: adapter.getStats.bind(adapter),
        bumpStats: adapter.bumpStats.bind(adapter),
        getMemory: adapter.getMemory.bind(adapter),
        putMemory: adapter.putMemory.bind(adapter),
        getWork: adapter.getWork.bind(adapter),
        putWork: (work: unknown) => {
          putWork(work);
          adapter.putWork(work as never);
        },
        creditWages: adapter.creditWages.bind(adapter),
      },
    });
    expect(port).not.toBeNull();
    creditPayday(port!, "Pip", [
      finishWage({
        dinoId: "tusksaw",
        factoryId: "fossil-pit",
        factoryName: "Fossil pit",
        shiftPay: 20,
        lifetimeEarnings: 20,
        hiredAt: "2026-01-01T00:00:00.000Z",
        mineralsFound: 0,
        mateRank: 0,
        employment: "tamed",
      }),
      finishWage({
        dinoId: "lanternmaw",
        factoryId: "scales-yard",
        factoryName: "Scales yard",
        shiftPay: 40,
        lifetimeEarnings: 40,
        hiredAt: "2026-01-01T00:00:00.000Z",
        mineralsFound: 0,
        mateRank: 0,
        employment: "tamed",
      }),
    ]);
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(120);
    expect(adapter.getWork("lanternmaw")?.lifetimeEarnings).toBe(40);
    expect(putWork).not.toHaveBeenCalled();
  });

  it("reads an empty core until Seed lab crew writes the demo", () => {
    const adapter = new LocalStorageAdapter(memoryKeyValueStore());
    const port = portFromCore({ storage: adapter });
    expect(port).not.toBeNull();
    expect(port!.briefs("Pip")).toEqual([]);
    expect(port!.wages("Pip")).toEqual([]);
    expect(port!.recall("lanternmaw")).toBe("");
    port!.setBrief("Pip", finishBrief({ familyId: "swamp", rep: 40, kills: 9 }));
    expect(adapter.getStats("Pip").find((row) => row.familyId === "swamp")).toBeUndefined();

    const forgotten: unknown[] = [];
    const amnesiac = portFromCore({
      storage: {
        getWork: () => null,
        getMemory: () => null,
        putMemory: (memory: unknown) => {
          forgotten.push(memory);
        },
      },
    });
    amnesiac!.remember("tusksaw", "secret");
    expect(amnesiac!.recall("tusksaw")).toBe("");
    expect(forgotten[0]).toMatchObject({ dinoId: "tusksaw", summary: "secret" });

    port!.remember("lanternmaw", "scales in the pocket");
    expect(port!.recall("lanternmaw")).toBe("scales in the pocket");

    port!.seedLabCrew("Pip");
    expect(port!.briefs("Pip").find((row) => row.familyId === "swamp")).toMatchObject({
      rep: -50,
      kills: 1,
      mangoRefused: 1,
      band: "hostile",
    });
    expect(port!.briefs("Pip").find((row) => row.familyId === "sickle")).toMatchObject({
      rep: 4,
      band: "neutral",
    });
    expect(port!.briefs("Pip").find((row) => row.familyId === "roost")).toMatchObject({
      rep: 36,
      mangoGiven: 2,
      band: "friendly",
    });
    expect(port!.wages("Pip").find((card) => card.dinoId === "lanternmaw")).toMatchObject({
      lifetimeEarnings: 40,
      employment: "tamed",
      factoryId: "scales-yard",
      shiftPay: 0,
    });
    expect(port!.wages("Pip").find((card) => card.dinoId === "chatty-bird")).toMatchObject({
      lifetimeEarnings: 0,
      factoryId: "",
      hiredAt: null,
      employment: null,
    });
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(20);
    port!.creditShift("Pip", "tusksaw", 20);
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(40);
    port!.seedLabCrew("Pip");
    expect(adapter.getWork("tusksaw")?.lifetimeEarnings).toBe(40);
    expect(port!.briefs("Pip").find((row) => row.familyId === "swamp")?.kills).toBe(1);

    port!.setWage("Pip", finishWage({
      dinoId: "ripwing",
      factoryId: "amber-kiln",
      factoryName: "Amber kiln",
      shiftPay: 10,
      lifetimeEarnings: 10,
      hiredAt: "2026-01-01T00:00:00.000Z",
      mineralsFound: 0,
      mateRank: 0,
      employment: "rented",
    }));
    expect(adapter.getWork("ripwing")?.employment).toBe("rented");
    expect(port!.wages("Pip").find((card) => card.dinoId === "ripwing")?.employment).toBe("rented");
  });
});

describe("payday", () => {
  it("brags, finds a mineral, complains, and looks for a job", () => {
    const result = run("payday");
    expect(result.planId).toBe("payday");
    expect(result.leaderId).toBe("scythepaw");
    expect(result.posts.map((post) => post.role)).toEqual([
      "brag-pay",
      "complain-work",
      "mineral-find",
      "job-seeking",
    ]);
    const brag = result.posts[0]!;
    expect(brag.link).toBe("root");
    expect(brag.content).toContain("paid 500");
    expect(brag.content).toContain("Lifetime earnings 500");
    expect(brag.content).toContain("Mate rank is 1");
    const complain = result.posts[1]!;
    expect(complain.authorId).toBe("ripwing");
    expect(complain.link).toBe("child");
    expect(complain.parentId).toBe(brag.id);
    expect(complain.content.toLowerCase()).toContain("work");
    const find = result.posts[2]!;
    expect(find.authorId).toBe("tusksaw");
    expect(find.link).toBe("side");
    expect(find.content).toContain("Fossil");
    const job = result.posts[3]!;
    expect(job.authorId).toBe("chatty-bird");
    expect(job.link).toBe("side");
    expect(job.content).toContain("looking for a job at Bone works");
    expect(result.allChat).toHaveLength(1);
    expect(result.allChat[0]?.speakerId).toBe("scythepaw");
    expect(result.allChat[0]?.text).toContain("Mate rank 1");
    expect(result.wages.find((card) => card.dinoId === "scythepaw")).toMatchObject({
      lifetimeEarnings: 500,
      mateRank: mateRank(result.wages, "scythepaw"),
    });
    expect(result.wages.find((card) => card.dinoId === "lanternmaw")?.mateRank).toBe(2);
    expect(result.wages.find((card) => card.dinoId === "chatty-bird")).toMatchObject({
      shiftPay: 0,
      lifetimeEarnings: 0,
      mateRank: 5,
      hiredAt: null,
    });
    expect(result.wages.find((card) => card.dinoId === "tusksaw")?.mineralsFound).toBe(1);
    for (const [index, post] of result.posts.entries()) {
      if (index === 0) continue;
      const earlier = result.posts.slice(0, index).map((row) => row.authorName);
      expect(earlier.some((name) => post.content.includes(name))).toBe(true);
    }
  });

  it("adds the shift to lifetime earnings and leaves a job seeker alone", () => {
    const first = run("payday");
    const second = run("payday", { wages: first.wages });
    expect(second.wages.find((card) => card.dinoId === "scythepaw")).toMatchObject({
      lifetimeEarnings: 1000,
      mateRank: 1,
    });
    expect(second.posts[0]?.content).toContain("Mate rank is 1");
    expect(second.wages.find((card) => card.dinoId === "chatty-bird")?.mateRank).toBe(mateRank(second.wages, "chatty-bird"));
    const looking = finishWage({
      dinoId: "chatty-bird",
      factoryId: "",
      factoryName: "",
      shiftPay: 12,
      lifetimeEarnings: 4,
      hiredAt: "2026-01-01T00:00:00.000Z",
      mineralsFound: 0,
      mateRank: 9,
    });
    expect(looking.shiftPay).toBe(0);
    expect(looking.hiredAt).toBeNull();
    expect(looking.lifetimeEarnings).toBe(4);
    expect(looking.mateRank).toBe(0);
    expect(looking.employment).toBeNull();
  });

  it("reads lifetime earnings from a work row and ignores a stored mate rank", () => {
    const port = portFromCore({
      storage: {
        getStats: () => [{ familyId: "swamp", rep: 0 }],
        getWork: (id: string) => id === "scythepaw"
          ? {
            dinoId: "scythepaw",
            employerId: "bone-works",
            wagesEarned: 20,
            lifetimeEarnings: 500,
            hiredAt: "2024-05-01T00:00:00.000Z",
            mineralsFound: 2,
          }
          : null,
      },
    });
    const stored = port?.wages("Pip").find((card) => card.dinoId === "scythepaw");
    expect(stored).toMatchObject({
      lifetimeEarnings: 500,
      mateRank: 1,
      factoryName: "Bone works",
      hiredAt: "2024-05-01T00:00:00.000Z",
      mineralsFound: 2,
      shiftPay: 0,
    });
    const paid = run("payday", { wages: [{ ...stored!, shiftPay: 500 }] });
    expect(paid.posts.find((post) => post.role === "brag-pay")?.content).toContain("paid 500");
    expect(paid.wages.find((card) => card.dinoId === "scythepaw")).toMatchObject({
      lifetimeEarnings: 1000,
      mateRank: mateRank(paid.wages, "scythepaw"),
    });
  });
});

describe("roles stay in turn order", () => {
  it("assigns block, hit, and screen without doubling a dino", () => {
    const roles = assignRoles("toll", "lanternmaw", LAB_TREE, roster, briefs());
    const ids = roles.map((role) => role.dinoId);
    expect(new Set(ids).size).toBe(ids.length);
    expect(roles.map((role) => role.role)).toEqual(["lead", "block-path", "take-hit", "screen", "greet"]);
    expect(ids[1]).toBe("tusksaw");
    expect(ids[2]).toBe("scythepaw");
    expect(ids[3]).toBe("ripwing");
  });
});
