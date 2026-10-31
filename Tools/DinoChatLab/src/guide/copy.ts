import type { WizardId } from "./wizardState";

export interface WizardStep {
  title: string;
  body: string;
  /** Set on the Dinobook event steps so the guide stays tied to the button row. */
  eventId?: string;
}

export interface WizardCopy {
  id: WizardId;
  title: string;
  eyebrow: string;
  reopenLabel: string;
  steps: readonly WizardStep[];
}

export interface ControlNote {
  name: string;
  meaning: string;
}

export interface PanelCopy {
  id: string;
  title: string;
  purpose: string;
  tryFirst: string;
  controls: readonly ControlNote[];
}

export const WIZARDS: Record<WizardId, WizardCopy> = {
  crew: {
    id: "crew",
    title: "Crew setup",
    eyebrow: "Crew",
    reopenLabel: "Open crew setup",
    steps: [
      {
        title: "Pick the dinos",
        body: "Bundled dinos are read-only. On Personas, Duplicate or + New makes a draft you can test before you save. Dinobook's Seed lab crew writes the demo payroll: Lanternmaw at the Scales yard, Tusksaw at the Fossil pit, Scythepaw at the Bone works, Ripwing at the Amber kiln, and Chatty Bird still looking. Big Grumble is the summon, not a shift.",
      },
      {
        title: "Personas",
        body: "The editor holds the name, the description, and a speech pattern: CASUAL, FORMAL, or EXPRESSIVE. Topic chips are like, dislike, or neutral. Relationships are friend, rival, or family, with affinity from -10 to 10. Interest minimums run from 1 to 10. The test chat speaks with ELIZA on the draft, saved or not.",
      },
      {
        title: "Wages",
        body: "Shift pay is what Payday adds, from 0 to 10000. Lifetime is the core total. It is read-only and only goes up. Mate rank is the crew place from mateRank. 1 is first pick, and it is not stored on the row.",
      },
      {
        title: "Jobs",
        body: "A factory is a lab job named for a material: Fossil pit, Amber kiln, Scales yard, or Bone works. Looking means the factory is blank, and the feed can say they are looking for a job. Once a factory is set, employment is Tamed or Rented. Both count on the crew.",
      },
    ],
  },
  events: {
    id: "events",
    title: "Dinobook events",
    eyebrow: "Events",
    reopenLabel: "Open event guide",
    steps: [
      {
        title: "What a button does",
        body: "Each button posts on the feed before anyone speaks in all-chat. An elder leads. Others take a role, or stay quiet when the speaker cap says so. The family dropdown is who receives the stat bump. Payday and Summon Big Grumble do not bump that row. The player name is the one in Talk. A blank name is you.",
      },
      {
        title: "Player joined",
        eventId: "player-joined",
        body: "Plan: welcome. The leader is the elder whose family has the highest rep, then the kindest alignment. Friends wave on side links. Nobody blocks the path. The button does not change rep, kills, or mango.",
      },
      {
        title: "Attacked",
        eventId: "attacked",
        body: "Counts one kill and one hit on the selected family, and drops rep by the kill delta (8). A harsh leader (LE, NE, CE, or CN) calls a toll: one dino blocks the path, one takes the hit, then the leader demands mango. Any other alignment covers and asks instead of extorting.",
      },
      {
        title: "Refused mango",
        eventId: "refused-mango",
        body: "Counts one refused mango and drops rep by 2 on the selected family. The leader is the elder whose family has been refused the most, then the harshest alignment. That leader's alignment picks a toll or a cover.",
      },
      {
        title: "Rental downed",
        eventId: "rental-downed",
        body: "Counts one downed rental and drops rep by 6. The leader is the most rentals downed, then the lowest rep. A harsh leader repossesses and tells the player to bring the rental back. A kind leader covers.",
      },
      {
        title: "Payday",
        eventId: "payday",
        body: "The shift bell. It does not change the family brief. creditWages adds each shift onto lifetime, and lifetime only goes up. Posts may brag about pay, note a mineral find, complain about the work, or say they are looking for a job. The highest shift speaks the payday line in all-chat.",
      },
      {
        title: "Summon Big Grumble",
        eventId: "summon-big-grumble",
        body: "Polls every elder. Each reply quotes that family's rep, kills, bounties, and rentals. Big Grumble's all-chat line follows those replies. The button itself does not add a kill or a rep change. Seed lab crew first when the briefs are empty, so the poll has numbers to quote.",
      },
    ],
  },
  traces: {
    id: "traces",
    title: "Reading traces",
    eyebrow: "Trace",
    reopenLabel: "Open trace guide",
    steps: [
      {
        title: "Engines",
        body: "ELIZA, AIML, RiveScript, and MegaHAL each try a line. They need no download. WebLLM is the context model. It stays quiet until you load one from Talk. The header switch Trace has to be on, or the extra engine calls are skipped and you only keep the chosen line.",
      },
      {
        title: "The badge",
        body: "Open Trace under a bubble. The coloured pill is the source that was kept: ELIZA, AIML, RiveScript, MegaHAL, WebLLM, Template, Fallback, Record, Alignment, or None. The card with the amber ring is that same pick. The others are what the rest of the engines said, or none.",
      },
      {
        title: "The reason line",
        body: "The sentence under the badge is the reason: scores, the PG filter, steer, alignment, a {Target} fill, and any fallback. Timings sit on each engine. The step list underneath is the same story in order, from fill to the pick.",
      },
      {
        title: "The #/trace log",
        body: "The Trace log at #/trace lists chat turns, Dinobook posts, paydays, and elder picks, oldest first, with a day rule when the date changes. Filter by dino or by source. Export JSON downloads the rows you are looking at. Clear log wipes this tab's list. It is the in-memory capture, not a scrape of the bubbles.",
      },
    ],
  },
};

export const PANELS: Record<string, PanelCopy> = {
  talk: {
    id: "talk",
    title: "Talk",
    purpose: "Choose who speaks, which classic engine writes the line, and whether a small context model steers or rephrases it.",
    tryFirst: "Leave the engine on ELIZA, stay on 1:1, and send “why you running”. The pack is empty and the player starts outside smell range, so it stays a chat line.",
    controls: [
      { name: "1:1 / All-chat / Game event", meaning: "One dino, every checked dino, or a single dino answering a game line." },
      { name: "Line engine", meaning: "ELIZA, AIML, RiveScript, or MegaHAL. Temperature does not change these." },
      { name: "Focus dino", meaning: "Whose prompt, alignment, and context window you are editing." },
      { name: "On the channel", meaning: "All-chat only. Checked dinos answer the same post." },
      { name: "Player name", meaning: "Fills {Target}. Blank leaves the token as you. Dinobook uses this name too." },
      { name: "Temperature and max tokens", meaning: "For the context model. Max tokens also clips the spoken line." },
      { name: "System prompt", meaning: "Edit it, then Rebuild prompt to pick up the live alignment and memory." },
      { name: "Reset chat / Export JSON", meaning: "Reset clears the transcript and summaries. Export writes a dino-chat-lab JSON file." },
      { name: "PG filter", meaning: "Shows what the filter swapped on a dino line, or that nothing changed." },
      { name: "Context model", meaning: "Load a small WebLLM prebuilt in the browser. Steer and Rephrase wait until that load finishes." },
    ],
  },
  transcript: {
    id: "transcript",
    title: "Transcript",
    purpose: "The paper sheet of the conversation. The classic engine writes the line. The slate next door is the decision.",
    tryFirst: "Press a preset, then Send. Open Trace under the reply to see the badge and the reason.",
    controls: [
      { name: "Presets", meaning: "Fill the box. They do not send by themselves." },
      { name: "Composer", meaning: "Enter sends. Shift+Enter keeps a new line. Player text clips at 200 characters." },
      { name: "Send", meaning: "The focused dino answers, or every checked dino in all-chat." },
      { name: "Another round", meaning: "All-chat only. Dinos hear the latest line. A dino does not answer the one who just spoke unless they are rivals or interest is high." },
      { name: "Trace under a bubble", meaning: "The badge, the reason line, every engine, and the step list. Hidden while the header Trace switch is off." },
    ],
  },
  decision: {
    id: "decision",
    title: "Decision",
    purpose: "The live alignment call for the focused dino: intent, reasons, and the next classic utterance.",
    tryFirst: "Press Tusksaw attack. The intent should leave ignore. Clear world puts the empty pack and the long distance back.",
    controls: [
      { name: "Alignment override", meaning: "Persona default, or a lab alignment for this session." },
      { name: "Intent, reasons, hint", meaning: "attack, ambush, barter, beg, protect, extort, or ignore, plus why, plus the lab sentence." },
      { name: "Inventory", meaning: "Item id, name, and count. Add mango block is the sample the dinos want." },
      { name: "Distance and smell", meaning: "Feet. Smell defaults to 30. Inside that range, a wanted material can change the intent." },
      { name: "Odds and Dinobook rally", meaning: "How sure the dino is of winning, and whether the feed can bring help." },
      { name: "Tusksaw attack / ambush / Clear world", meaning: "Attack is mango, close range, high odds, no rally. Ambush is the same pack with low odds and rally on." },
      { name: "Context budget", meaning: "Separate windows for 1:1 or events, and for all-chat. The meter is the focused dino." },
      { name: "Cooldowns and thresholds", meaning: "Rest, caps, speak chance, and the interest minimums that keep a dino quiet." },
      { name: "Relationship, elder, family", meaning: "How this dino treats the named player, and whether an elder reads the player record." },
      { name: "World events", meaning: "Write a stat for the current player name. They are not wired to the game." },
    ],
  },
  dinobook: {
    id: "dinobook",
    title: "Dinobook",
    purpose: "A lab feed, not a social system in the game. Elders post, families reply under them, and friends sit on side links. Dinos read the feed before all-chat.",
    tryFirst: "Seed lab crew, then press Player joined. The thread fills before the all-chat strip.",
    controls: [
      { name: "Stat bump applies to", meaning: "Which family receives kills, mango, rentals, and rep. Payday and Summon skip this." },
      { name: "Event buttons", meaning: "Player joined, Attacked, Refused mango, Rental downed, Payday, and Summon Big Grumble. The event guide says what each one changes." },
      { name: "Tree", meaning: "Swamp, Sickle, and Roost, plus side links. A posting order, not a biography." },
      { name: "All-chat budget", meaning: "How many lines, tokens, and speakers leave the feed. Only the leader speaks in all-chat when that box is checked." },
      { name: "Seed lab crew", meaning: "Writes the demo payroll and family stats into the core." },
      { name: "Wages", meaning: "Shift pay, read-only lifetime, factory, and tamed or rented." },
      { name: "Elder briefs", meaning: "What each family would tell Big Grumble. Disabled when the memory core is the only record and the field is locked." },
      { name: "Clear feed / Reset stats", meaning: "Clear feed drops the posts. Reset stats drops the local payroll and briefs." },
    ],
  },
  "dinobook-feed": {
    id: "dinobook-feed",
    title: "Feed",
    purpose: "The thread for the events you have pressed, then the all-chat lines the budget allowed out.",
    tryFirst: "Press one event and read the elder post before the nested replies. Held back names sit under that event's all-chat.",
    controls: [
      { name: "Day rule", meaning: "A date line when the calendar day changes." },
      { name: "Event header", meaning: "The button name, the time, and the plan summary." },
      { name: "Posts", meaning: "Elder, family, or side link, with a role such as brag, mineral, complaint, or job." },
      { name: "All chat", meaning: "The spoken lines. Quiet means the budget kept the rest on the feed." },
      { name: "Held back", meaning: "Who did not speak, and the reason." },
    ],
  },
  username: {
    id: "username",
    title: "Username",
    purpose: "Names this browser so personas and memory have an owner. There is no password.",
    tryFirst: "Type 3 to 20 characters and press Use this name. The roster opens behind this card.",
    controls: [
      { name: "Username", meaning: "Saved in local storage and a cookie. Lose the cookie and you pick a new name. Dinos saved under the old name stay put." },
      { name: "Use this name", meaning: "Local play has no daily cap. A shared hub, when configured, allows 100 new names per UTC day." },
    ],
  },
  "dino-list": {
    id: "dino-list",
    title: "Dinos",
    purpose: "The roster. Bundled dinos ship with the lab. Yours are the ones you duplicate or create.",
    tryFirst: "Select a bundled dino, then Duplicate it. The copy is yours and the editor unlocks.",
    controls: [
      { name: "Search", meaning: "Filters by name or description." },
      { name: "Speech pattern", meaning: "All patterns, or CASUAL, FORMAL, or EXPRESSIVE." },
      { name: "+ New", meaning: "A blank draft in My dinos." },
      { name: "Duplicate", meaning: "Copies the selected dino into a draft you can edit and save." },
      { name: "Groups", meaning: "Bundled, My dinos, and Shared. Bundled stays read-only." },
    ],
  },
  editor: {
    id: "editor",
    title: "Editor",
    purpose: "The persona the test chat and, once saved, the roster will use.",
    tryFirst: "Duplicate a dino, change one topic to like, and mention that word in the test chat.",
    controls: [
      { name: "Name and description", meaning: "What the roster shows and how the dino is described." },
      { name: "Speech pattern and style", meaning: "CASUAL, FORMAL, or EXPRESSIVE, plus a short style line." },
      { name: "Topics", meaning: "Like, dislike, or neutral. One word works best in the test chat." },
      { name: "Relationships", meaning: "Friend, rival, or family, and affinity from -10 to 10." },
      { name: "Minimum interest", meaning: "1 to 10 for engagement, post, comment, and like. The test chat replies above the comment minimum." },
      { name: "Save / Delete", meaning: "Yours only. Bundled rows stay read-only." },
      { name: "Export / Import JSON", meaning: "A persona file. Import adds a draft." },
    ],
  },
  "test-chat": {
    id: "test-chat",
    title: "Test chat",
    purpose: "ELIZA speaks the draft, including unsaved edits, and the log underneath is the decision.",
    tryFirst: "Say hello, or name a topic this dino likes. Then read the decision log and the two memory blocks.",
    controls: [
      { name: "Player name", meaning: "Whose relationship memory and rep this chat uses." },
      { name: "Send / Reset chat", meaning: "Send one ELIZA turn. Reset chat clears the lines, not the saved persona." },
      { name: "Decision log", meaning: "Action, interest, intent, quota, the reason, and the memory note." },
      { name: "Memory", meaning: "Rep band and lifetime earnings, the dino summary, and the summary with this player." },
      { name: "Reset memory", meaning: "Clears those notes for the current player name." },
    ],
  },
  "trace-log": {
    id: "trace-log",
    title: "Trace log",
    purpose: "Every captured turn in this tab, in time order: chat, Dinobook posts, paydays, and elder picks.",
    tryFirst: "Say something in the lab or press a Dinobook event, then come back here. Filter to one dino.",
    controls: [
      { name: "Trace switch", meaning: "Off skips extra engine calls. Older probes stay in the list." },
      { name: "Dino", meaning: "All dinos, or one speaker." },
      { name: "Source", meaning: "All sources, or one badge: ELIZA, AIML, RiveScript, MegaHAL, WebLLM, Template, Fallback, Record, Alignment, or None." },
      { name: "Export JSON", meaning: "Downloads the rows the filters are showing." },
      { name: "Clear log", meaning: "Wipes this tab's capture. It does not edit the transcript bubbles." },
    ],
  },
};

export const PANEL_IDS = Object.keys(PANELS);
