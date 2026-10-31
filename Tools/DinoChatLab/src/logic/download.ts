/** Long enough for the browser to start a blob download before the URL dies. */
export const DOWNLOAD_URL_HOLD_MS = 1000;

export function revokeObjectUrlLater(url: string, holdMs = DOWNLOAD_URL_HOLD_MS): void {
  setTimeout(() => {
    URL.revokeObjectURL(url);
  }, holdMs);
}
