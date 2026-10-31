import { computed, onScopeDispose, ref } from "vue";
import { findTrace, listTraces, setTraceEnabled, subscribeTraces, traceEnabled } from "../logic/trace";
import type { TraceEvent } from "../logic/traceTypes";

export function useTrace() {
  const tick = ref(0);
  const stop = subscribeTraces(() => {
    tick.value += 1;
  });
  onScopeDispose(stop);

  const enabled = computed({
    get() {
      tick.value;
      return traceEnabled();
    },
    set(on: boolean) {
      setTraceEnabled(on);
    },
  });

  const events = computed(() => {
    tick.value;
    return listTraces();
  });

  function byId(id?: string): TraceEvent | null {
    tick.value;
    if (!id) return null;
    return findTrace(id);
  }

  return { enabled, events, byId };
}
