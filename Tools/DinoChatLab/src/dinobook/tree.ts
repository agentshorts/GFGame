/**
 * Lab posting tree. It is not a biography and it is not in the game data.
 * Elders post first. Family members reply under their elder.
 * Friendly dinos attach as side links instead of joining that family thread.
 */

export interface FamilyNode {
  familyId: string;
  name: string;
  elderId: string;
  /** Elder included. */
  memberIds: readonly string[];
}

export interface SideLink {
  a: string;
  b: string;
}

export interface FamilyTree {
  families: readonly FamilyNode[];
  sideLinks: readonly SideLink[];
}

export const BIG_GRUMBLE_ID = "big-grumble";

export const LAB_TREE: FamilyTree = {
  families: [
    { familyId: "swamp", name: "Swamp", elderId: "lanternmaw", memberIds: ["lanternmaw", "tusksaw"] },
    { familyId: "sickle", name: "Sickle", elderId: "scythepaw", memberIds: ["scythepaw", "ripwing"] },
    { familyId: "roost", name: "Roost", elderId: "chatty-bird", memberIds: ["chatty-bird"] },
  ],
  sideLinks: [
    { a: "tusksaw", b: "ripwing" },
    { a: "scythepaw", b: "chatty-bird" },
    { a: "ripwing", b: "chatty-bird" },
  ],
};

export function familyOf(tree: FamilyTree, dinoId: string): FamilyNode | null {
  return tree.families.find((family) => family.memberIds.includes(dinoId)) ?? null;
}

export function isElder(tree: FamilyTree, dinoId: string): boolean {
  return tree.families.some((family) => family.elderId === dinoId);
}

export function eldersOf(tree: FamilyTree): FamilyNode[] {
  return tree.families.filter((family) => family.elderId.length > 0);
}

export function friendsOf(tree: FamilyTree, dinoId: string): string[] {
  const ids: string[] = [];
  for (const link of tree.sideLinks) {
    if (link.a === dinoId) ids.push(link.b);
    else if (link.b === dinoId) ids.push(link.a);
  }
  return ids;
}

export function areFriends(tree: FamilyTree, a: string, b: string): boolean {
  return friendsOf(tree, a).includes(b);
}
