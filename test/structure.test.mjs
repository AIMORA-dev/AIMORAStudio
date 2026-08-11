import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("Studio owns a versioned public protocol without engineering semantics", async () => {
  const source = await readFile(new URL("../protocol/src/index.ts", import.meta.url), "utf8");
  assert.match(source, /AIMORA_PROTOCOL_REVISION/);
  assert.match(source, /AIMORAProtocolEnvelope/);
  assert.doesNotMatch(source, /solver.*source/i);
});
