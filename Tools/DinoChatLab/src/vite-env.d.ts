/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_DINO_API?: string;
}

declare module "*.vue" {
  import type { DefineComponent } from "vue";
  const component: DefineComponent<object, object, unknown>;
  export default component;
}

declare module "rivescript" {
  export default class RiveScript {
    constructor(options?: { utf8?: boolean });
    stream(source: string): Promise<void>;
    sortReplies(): void;
    reply(user: string, message: string): Promise<string>;
  }
}
