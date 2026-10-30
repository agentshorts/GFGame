# Dino Chat Lab

A local Vue 3 workbench for talking to the game's dinos in the browser. Classic chatbot engines write the lines. A small WebLLM model can sit in front of them as a memory layer. Nothing here is wired into Unreal.

```bash
cd Tools/DinoChatLab
npm install
npm run dev
```

Open the URL Vite prints (it serves `http://127.0.0.1:5173` by default).

`npm test` runs the Vitest suite. `npm run typecheck` checks Vue and TypeScript. `npm run build && npm run preview` is the static build. A second no-toolchain HTML page is not included: the UI is Vue single-file components, and a hand-written twin would drift from this one.

## What speaks

ELIZA, AIML, RiveScript, and MegaHAL produce the dino's line. They need no download. ELIZA is the default.

WebLLM (`@mlc-ai/web-llm`) is the context layer, not the default voice. It keeps a configurable window (default **4096** estimated tokens, clamped from 128 to 8192) of the chat and the world note, summarizes when that budget fills, and can optionally pick an intent or rephrase the classic line. The meter on the slate panel is that budget. The estimate is four characters to a token. When a model actually runs, its own tokens/sec and runtime stats are shown too.

Until a model is loaded, the steer and rephrase checkboxes do nothing and the classic line is what you hear. That path is the one the tests cover.

## WebGPU

The context model needs WebGPU:

- A current Chromium, Edge, or Firefox build with WebGPU enabled.
- `navigator.gpu.requestAdapter()` has to return an adapter. The lab prints the result.
- The dev server and the preview server send `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`, which WebLLM uses for `SharedArrayBuffer`.
- The first load downloads a small prebuilt (Qwen2.5 0.5B Instruct is the default, plus Qwen2.5 1.5B, Llama 3.2 1B and 3B, SmolLM2 135M / 360M / 1.7B, and Phi-3.5-mini). Weights stay in the browser cache. Do not commit them.

If WebGPU is missing, use ELIZA, AIML, RiveScript, or MegaHAL. They are the line engines either way.

## Packages

| Piece | Package | Why this one |
| --- | --- | --- |
| ELIZA | `eliza-core` | Each persona loads its own script. `elizabot` bakes in one doctor script. |
| AIML | `aiml.js` | Loads `{ name, content }` in the browser. `aiml-high` reads the filesystem. |
| RiveScript | `rivescript` | Trigger and reply script, streamed from a string. |
| Extra chatter | `megahal` | There is no Colby PARRY chatbot on npm. The `parry` package is an unrelated validator, so it is not used. |
| Filter | `obscenity` | PG pass on outputs, with a toggle that shows the spans. |
| Context model | `@mlc-ai/web-llm` | Worker + `CreateWebWorkerMLCEngine`, same shape as RoundTable, on current package versions. |
| ELIZA's stream | `rxjs` 6 | `eliza-core` imports RxJS 6. One hoisted copy is pinned so a second copy is not nested. |

The WebLLM loading pattern (worker, progress callback, `chat.completions.create`, prebuilt model list) is borrowed from Captain's public project [RoundTable](https://gitlab.com/atomicbreathgodzilla/RoundTable). This lab does not copy that app, and nothing is pushed back to that GitLab repo.

`npm audit` reports issues in old transitive packages under RiveScript. They were left as installed. Do not run `npm audit fix --force`.

## Personas

JSON files in `personas/`. Spoken lines for Big Grumble are copied from `DT_GiantDinoLines.csv`. Tusksaw, Ripwing, Scythepaw, and Lanternmaw only restate phrases from `docs/NewDinos_v1.md`. Chatty Bird is a lab stand-in: the repo has no named chatty bird, and Ripwing is not that bird. `loreTodo` marks every gap. Do not invent a biography.

Lab alignments (not in the game data): Big Grumble CN, Tusksaw NE, Ripwing CN, Scythepaw CG, Lanternmaw LE, Chatty Bird NG. Each persona's want is a **mango block**, a sample material. It is not a taming food. Grub Jelly, Fizzberry, Petit Cake, and Golden Carp stay the favourites on the design sheet.

## Decision debug

The slate panel runs `decide()` live. Edit the inventory, the distance, the smell range (default 30 ft), the dino's own odds, and whether Dinobook can rally help. The intent is attack, ambush, barter, beg, protect, extort, or ignore, with the reasons and a speech hint.

A neutral-evil dino that smells a wanted material within 30 ft attacks when it thinks it can win, or sets an ambush when Dinobook can bring help, or begs: "yo I can smell those mango blocks you got, can you spare one". Lawful evil can extort: "just drop some and we won't attack". The hint is the lab sentence. Big Grumble's spoken cue is still a canned CSV line.

The chat demo starts with an empty pack and the player 40 ft away, so "why you running" stays a chat cue.

## Modes and controls

- 1:1 chat with the focused dino.
- All-chat: checked dinos share one context window and each answers the same post. Another round hears the latest line.
- Game event: one dino answers a line such as "player ran away". Move the player outside smell range if you want the event text, not the world intent, to steer the script.

Also: temperature (context model only), max tokens, system prompt, reset, and JSON export (kind `dino-chat-lab`, version 2). Player chat text is clipped to 200 characters, matching `MaxChatChars`. Replies are filled for `{Target}`, filtered, then clipped.

See `HANDOFF_dino-chat-lab.md` for how a later Unreal hook could use an export without running a model in the game.
