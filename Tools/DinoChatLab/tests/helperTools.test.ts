import { describe, expect, it } from "vitest";
import {
  HELPER_TOOL_IDS,
  LAB_LOCOMOTION,
  bearerFor,
  helperCatalog,
  helperSystemLine,
  helperToolSchemas,
  helperToolsFor,
  readHelperCall,
  resolveHelperCall,
  unpromptedOffer,
} from "../src/logic/helperTools";

const offers = {
  catch_player: "Want me to catch you?",
  float_water_area: "Want me to float the water area?",
  fly_player_over: "Want me to just fly you over?",
} as const;

describe("helper catalog", () => {
  it("keeps the three tools from decision 19, in offer order", () => {
    expect(HELPER_TOOL_IDS).toEqual(["catch_player", "float_water_area", "fly_player_over"]);
    expect(helperCatalog().map((tool) => tool.offer)).toEqual([
      offers.catch_player,
      offers.float_water_area,
      offers.fly_player_over,
    ]);
    expect(helperCatalog().map((tool) => tool.behaviorId)).toEqual([
      "catch-player",
      "float-water-area",
      "fly-player-over",
    ]);
    expect(helperCatalog().map((tool) => tool.locomotion)).toEqual(["flyer", "water", "flyer"]);
  });

  it("lets flyers carry, lets water float, and leaves everyone else on the ground", () => {
    expect(helperToolsFor(bearerFor("ripwing")).map((tool) => tool.id)).toEqual([
      "catch_player",
      "fly_player_over",
    ]);
    expect(helperToolsFor(bearerFor("lanternmaw")).map((tool) => tool.id)).toEqual(["float_water_area"]);
    for (const id of ["tusksaw", "scythepaw", "big-grumble", "chatty-bird", "sailback"]) {
      expect(helperToolsFor(bearerFor(id))).toEqual([]);
    }
    expect(LAB_LOCOMOTION["chatty-bird"]).toBe("land");
    expect(helperToolsFor(bearerFor("ripwing", "land"))).toEqual([]);
    expect(helperToolsFor(bearerFor("tusksaw", ["water", "flyer"])).map((tool) => tool.id)).toEqual([
      "catch_player",
      "float_water_area",
      "fly_player_over",
    ]);
    expect(helperToolsFor({ id: "mystery", locomotion: "swim" as "land" })).toEqual([]);
  });
});

describe("schemas and offers", () => {
  it("builds a function schema only for tools that dino can call", () => {
    const schemas = helperToolSchemas(bearerFor("ripwing"));
    expect(schemas.map((tool) => tool.function.name)).toEqual(["catch_player", "fly_player_over"]);
    expect(schemas[0]?.function.parameters.required).toEqual([]);
    expect(schemas[1]?.function.parameters.required).toEqual(["level"]);
    expect(schemas[1]?.function.parameters.additionalProperties).toBe(false);
    expect(helperToolSchemas(bearerFor("lanternmaw"))[0]?.function.parameters.required).toEqual(["area"]);
    expect(helperToolSchemas(bearerFor("tusksaw"))).toEqual([]);
  });

  it("names only the offered tools in the system line", () => {
    const flyer = helperSystemLine(bearerFor("ripwing"));
    expect(flyer).toContain("catch_player, fly_player_over");
    expect(flyer).not.toContain("float_water_area");
    expect(flyer).toContain("fly_player_over needs level");
    const water = helperSystemLine(bearerFor("lanternmaw"));
    expect(water).toContain("float_water_area");
    expect(water).not.toContain("catch_player");
    expect(helperSystemLine(bearerFor("tusksaw"))).toBe(
      "This dino cannot offer a helper tool. Reply {\"name\":null}.",
    );
  });

  it("volunteers the tools that dino actually has", () => {
    expect(unpromptedOffer(bearerFor("ripwing"), " platform 3 ")).toBe(
      "I can see you're on platform 3, do you want me to catch you or just fly you over?",
    );
    expect(unpromptedOffer(bearerFor("lanternmaw"))).toBe(
      "I can see you're on this level, do you want me to float the water area?",
    );
    expect(unpromptedOffer(bearerFor("tusksaw", ["water", "flyer"]), "level X")).toBe(
      "I can see you're on level X, do you want me to catch you, float the water area, or just fly you over?",
    );
    expect(unpromptedOffer(bearerFor("scythepaw"))).toBeNull();
    expect(unpromptedOffer(bearerFor("ripwing"), " \u0000 ")).toBe(
      "I can see you're on this level, do you want me to catch you or just fly you over?",
    );
  });
});

describe("model tool calls", () => {
  it("reads a bare call, a fenced call, and an OpenAI tool_calls payload", () => {
    const flyer = bearerFor("ripwing");
    const bare = resolveHelperCall(flyer, 'Sure. {"name":" fly_player_over ","arguments":{"level":"X"}}');
    expect(bare).toEqual({
      ok: true,
      kind: "loaded",
      behavior: {
        tool: "fly_player_over",
        behaviorId: "fly-player-over",
        offer: offers.fly_player_over,
        args: { level: "X" },
      },
    });

    const fenced = resolveHelperCall(flyer, [
      "```json",
      '{"name":"catch_player","arguments":{"where":" the ledge "}}',
      "```",
    ].join("\n"));
    expect(fenced.ok && fenced.kind === "loaded" && fenced.behavior.args).toEqual({ where: "the ledge" });
    expect(fenced.ok && fenced.kind === "loaded" && fenced.behavior.offer).toBe(offers.catch_player);

    const openAi = resolveHelperCall(bearerFor("lanternmaw"), JSON.stringify({
      role: "assistant",
      content: null,
      tool_calls: [{
        id: "call_1",
        type: "function",
        function: { name: "float_water_area", arguments: "{\"area\":\"lily pond\"}" },
      }],
    }));
    expect(openAi.ok && openAi.kind === "loaded" && openAi.behavior.behaviorId).toBe("float-water-area");
    expect(openAi.ok && openAi.kind === "loaded" && openAi.behavior.args).toEqual({ area: "lily pond" });

    const objectArgs = resolveHelperCall(bearerFor("lanternmaw"), JSON.stringify({
      tool_calls: [{ function: { name: "float_water_area", arguments: { area: "reed bed" } } }],
    }));
    expect(objectArgs.ok && objectArgs.kind === "loaded" && objectArgs.behavior.args).toEqual({ area: "reed bed" });
  });

  it("keeps a brace inside a string and coerces a numeric level", () => {
    const call = resolveHelperCall(
      bearerFor("ripwing"),
      '{"name":"fly_player_over","arguments":{"level":"a {b} c"}}',
    );
    expect(call.ok && call.kind === "loaded" && call.behavior.args.level).toBe("a {b} c");
    const numeric = resolveHelperCall(
      bearerFor("ripwing"),
      '{"name":"fly_player_over","arguments":{"level":3}}',
    );
    expect(numeric.ok && numeric.kind === "loaded" && numeric.behavior.args.level).toBe("3");
  });

  it("uses the first call and declines when the model says null", () => {
    const text = [
      '{"name":"catch_player","arguments":{"where":"ledge","extra":"nope"}}',
      '{"name":"fly_player_over","arguments":{"level":"X"}}',
    ].join("\n");
    const first = resolveHelperCall(bearerFor("ripwing"), text);
    expect(first.ok && first.kind === "loaded" && first.behavior.tool).toBe("catch_player");
    expect(first.ok && first.kind === "loaded" && first.behavior.args).toEqual({ where: "ledge" });

    const decline = resolveHelperCall(bearerFor("ripwing"), 'I cannot help. {"name":null} {"name":"catch_player","arguments":{}}');
    expect(decline).toEqual({ ok: true, kind: "decline" });
    expect(readHelperCall("help player with platform level X")).toEqual({ found: false });
  });

  it("refuses unknown tools, tools this dino cannot offer, and bad arguments", () => {
    expect(resolveHelperCall(bearerFor("ripwing"), "please catch me")).toMatchObject({ ok: false, kind: "no-call" });
    expect(resolveHelperCall(bearerFor("ripwing"), "")).toMatchObject({ ok: false, kind: "no-call" });
    expect(resolveHelperCall(bearerFor("ripwing"), '{"name":"dance"}')).toMatchObject({
      ok: false,
      kind: "unknown-tool",
      detail: "dance",
    });
    expect(resolveHelperCall(bearerFor("lanternmaw"), '{"name":"fly_player_over","arguments":{"level":"X"}}')).toMatchObject({
      ok: false,
      kind: "not-offered",
    });
    expect(resolveHelperCall(bearerFor("lanternmaw"), '{"name":"float_water_area","arguments":{}}')).toMatchObject({
      ok: false,
      kind: "bad-args",
      detail: "area is required",
    });
    expect(resolveHelperCall(bearerFor("ripwing"), '{"name":"fly_player_over","arguments":"nope"}')).toMatchObject({
      ok: false,
      kind: "bad-args",
    });
    expect(resolveHelperCall(bearerFor("ripwing"), '{"name":"catch_player","arguments":{"where":"line\\nbreak"}}')).toMatchObject({
      ok: false,
      kind: "bad-args",
    });
    const huge = "L".repeat(65);
    expect(resolveHelperCall(bearerFor("ripwing"), JSON.stringify({
      name: "fly_player_over",
      arguments: { level: huge },
    }))).toMatchObject({ ok: false, kind: "bad-args", detail: "level is too long" });
    expect(resolveHelperCall(bearerFor("ripwing"), '{"name":"catch_player","arguments":{"where":true}}')).toMatchObject({
      ok: false,
      kind: "bad-args",
    });
  });

  it("ignores a call that starts past the scan window", () => {
    const call = `{"name":"catch_player","arguments":{}}`;
    const buried = `${"x".repeat(20_000)}${call}`;
    expect(resolveHelperCall(bearerFor("ripwing"), buried)).toMatchObject({ ok: false, kind: "no-call" });
    const visible = `${"hello ".repeat(30)}${call}`;
    expect(resolveHelperCall(bearerFor("ripwing"), visible).ok).toBe(true);
  });
});
