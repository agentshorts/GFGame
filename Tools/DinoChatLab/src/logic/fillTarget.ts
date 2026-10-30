/**
 * Same replacement as FGFGiantRules::FillTarget in Source/GFGiant/Private/GFGiantTypes.cpp.
 * A name replaces {Target}. With no name, the token and its lead-in are dropped, and a bare token becomes "you".
 */
const TOKEN = "{Target}";

function replaceAllCi(input: string, needle: string, replacement: string): string {
  const escaped = needle.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return input.replace(new RegExp(escaped, "gi"), replacement);
}

export function fillTarget(line: string, name: string): string {
  if (name.length > 0) {
    return replaceAllCi(line, TOKEN, name);
  }
  let out = line;
  out = replaceAllCi(out, `, ${TOKEN}`, "");
  out = replaceAllCi(out, ` ${TOKEN}.`, "");
  out = replaceAllCi(out, ` ${TOKEN}`, "");
  out = replaceAllCi(out, TOKEN, "you");
  return out;
}
