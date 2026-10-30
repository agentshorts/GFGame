# Handoff: Dino Chat Lab

The lab lives in `Tools/DinoChatLab`. It is a static Vue app. It does not link Unreal, and it must not be built into the dedicated server or the shipping client.

## Run

```bash
cd Tools/DinoChatLab
npm install
npm test
npm run dev
```

`npm run build && npm run preview` serves the production bundle. WebGPU is only needed when you press **Load context model**. ELIZA answers with no download. Browser notes are in `README.md`.

The decision panel updates before you send. Sample buttons set Tusksaw, three mango blocks, and 12 ft. Attack uses odds 0.8 and Dinobook off. Ambush uses odds 0.2 and Dinobook on. **Clear world** restores the empty pack at 40 ft so chat cues work again.

## Two chats in the game

Player all-chat and the giant's lines are different logs.

- Player chat: `AGFGameState::ServerHandleChat` appends `FGFChatMessage` (`Seq`, `SenderKey`, `SenderName`, `Text`, `Channel`) onto `ChatLog`. The channel used in the wild for this event is All. `MaxChatChars` is 200. The lab clips the composer to that and stamps every exported message with channel `All`.
- Giant lines: `UGFGiantEventComponent::Apply` appends `FGFGiantChatLine` (`Seq`, `LineId`, `Phase`, `Text`, `Speaker`, `Channel`, `TargetKey`, `At`). The speaker is the giant's display name. The channel comes from `DT_GiantDino` (All for Big Grumble). Phases include Spotted, Chase, CloseCall, Herded, Circling, Leaving, Return, Snacked, and Target.

`fillTarget` in `src/logic/fillTarget.ts` is a port of `FGFGiantRules::FillTarget`. A name replaces `{Target}`. An empty name drops the token with its lead-in, and a leftover token becomes "you". The Vitest cases follow that function, including `Hey {Target}!` with no name becoming `Hey!`.

## What not to wire yet

WebLLM downloads multi-hundred-megabyte weights into the browser cache and needs WebGPU. Do not call it from the dedicated server, a listen server, or the shipping client. Designers can export the lab transcript (JSON, version 2) and copy a line they like into `Data/CSV/DT_GiantDinoLines.csv` by hand. The export is a conversation, not a data-table row.

Alignment, Dinobook, smell range, win odds, and mango blocks are lab inputs. They are not columns on `DT_GiantDino`, not items in `DT_Items`, and not a social system in the repo. `decide()` in `src/logic/alignment.ts` is the whole rule set. It is pure TypeScript and covered by Vitest. A game version would be a new design, not a port of this file.

The other dinos have no spoken rows. Their lab lines only echo `docs/NewDinos_v1.md`. Chatty Bird has an empty `loreFromRepo` on purpose.

## A later hook, if someone wants one

1. Keep the giant component as the speaker for Big Grumble. His cues in this lab already use CSV strings, so an export should not be required for him.
2. For a designer pass on a new dino, run the lab, export JSON, and paste the chosen `text` into a new line row. Leave `{Target}` in the sheet when the line addresses the player. The game already fills that token.
3. A game-event string from the lab ("player ran away", "player attacked Big Grumble", "new player joined") is only a keyword for the classic script. The real event already has `EGFGiantLinePhase`. Passing that phase name into a future bridge would be the honest input. This lab does not subscribe to those delegates.
4. All-chat trash talk between dinos is local to the lab's one window. It does not write `ChatLog` or the giant line log.
5. If a context model is ever used by a tool again, keep it in this web lab. The game server should keep serving canned lines.

## Tests

`npm test` covers FillTarget, the PG filter, persona canon (CSV and `NewDinos_v1.md`), prompts, clipping, the small-model picker against the installed WebLLM list, ELIZA / AIML / RiveScript cue lines, a raw ELIZA override, MegaHAL, transcript version 2, the alignment matrix, the 4096 context budget, a mocked WebLLM client, and the memory core (per-dino budgets, summaries, quotas, the no-reply-to-reply rule, stat rows, the elder digest, and the localStorage adapter). The real model is not downloaded in CI-less local tests.

## Memory

Each dino has a 4096-token window. The shared transcript on screen is only the log. WebLLM is given that dino's turns, trimmed by `buildWebLlmMessages`. Summaries, relationships, player stats, knobs, and the decision log are in `localStorage` (`dcl:memory:`, `dcl:rel:`, `dcl:stats:`, `dcl:log`, `dcl:knobs`). `src/core/storage.ts` exports `SQLITE_SCHEMA` with the same columns. No worker is deployed from this slice, and no Unreal bridge posts the stat events. The slate's world-event buttons are the only writer.

`dino_work` is a hook only: employer factory, rented or tamed, wages, minerals, and a job-seeking flag. Mate rank is computed by `mateRank` and is not a column. There is no payroll and no job dialogue.
