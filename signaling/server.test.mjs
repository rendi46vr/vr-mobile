import assert from "node:assert/strict";
import {
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, before, test } from "node:test";
import { createSignalingServer } from "./server.mjs";

let server;
let endpoint;

before(async () => {
  server = createSignalingServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  endpoint = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await new Promise((resolve, reject) =>
    server.close((error) => error ? reject(error) : resolve()));
});

async function json(path, options = {}) {
  return jsonAt(endpoint, path, options);
}

async function jsonAt(base, path, options = {}) {
  const response = await fetch(`${base}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(options.headers || {}),
    },
  });
  return { status: response.status, body: await response.json() };
}

test("pairs once and reports presence", async () => {
  const created = await json("/v1/pair/create", {
    method: "POST",
    body: JSON.stringify({
      desktopId: "desktop-test-1",
      desktopName: "Test Laptop",
    }),
  });
  assert.equal(created.status, 201);
  assert.match(created.body.code, /^\d{10}$/);

  const claimed = await json("/v1/pair/claim", {
    method: "POST",
    body: JSON.stringify({
      code: created.body.code,
      deviceId: "device-test-1",
      deviceName: "Test Phone",
    }),
  });
  assert.equal(claimed.status, 200);

  const status = await json(
    `/v1/pair/status?sessionId=${created.body.sessionId}`,
    {
      headers: {
        Authorization: `Bearer ${created.body.desktopSecret}`,
      },
    },
  );
  assert.equal(status.body.paired, true);
  assert.equal(status.body.linkToken, claimed.body.linkToken);

  const phonePresence = await json("/v1/presence", {
    method: "POST",
    headers: { Authorization: `Bearer ${claimed.body.linkToken}` },
    body: JSON.stringify({ role: "device", peerId: "device-test-1" }),
  });
  assert.equal(phonePresence.status, 200);
  assert.equal(phonePresence.body.peerOnline, false);

  const desktopPresence = await json("/v1/presence", {
    method: "POST",
    headers: { Authorization: `Bearer ${claimed.body.linkToken}` },
    body: JSON.stringify({ role: "desktop", peerId: "desktop-test-1" }),
  });
  assert.equal(desktopPresence.body.peerOnline, true);

  const frameBytes = Buffer.from([0xff, 0xd8, 0xff, 0xd9]);
  const published = await fetch(`${endpoint}/v1/relay/frame`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${claimed.body.linkToken}`,
      "Content-Type": "image/jpeg",
      "X-VR-Peer-ID": "device-test-1",
    },
    body: frameBytes,
  });
  assert.equal(published.status, 204);

  const viewed = await fetch(`${endpoint}/v1/relay/frame`, {
    headers: {
      Authorization: `Bearer ${claimed.body.linkToken}`,
      "X-VR-Peer-ID": "desktop-test-1",
    },
  });
  assert.equal(viewed.status, 200);
  assert.deepEqual(Buffer.from(await viewed.arrayBuffer()), frameBytes);
  assert.equal(viewed.headers.get("x-frame-sequence"), "1");
});

test("rejects insecure identity and reused code", async () => {
  const invalid = await json("/v1/pair/create", {
    method: "POST",
    body: JSON.stringify({ desktopId: "x", desktopName: "Laptop" }),
  });
  assert.equal(invalid.status, 400);
});

test("creates a missing state directory before completing claim", async () => {
  const temporary = mkdtempSync(join(tmpdir(), "vr-mobile-signaling-"));
  const statePath = join(temporary, "nested", "signaling-state.json");
  const persistentServer = createSignalingServer({ statePath });
  try {
    await new Promise((resolve) =>
      persistentServer.listen(0, "127.0.0.1", resolve));
    const base = `http://127.0.0.1:${persistentServer.address().port}`;
    const created = await jsonAt(base, "/v1/pair/create", {
      method: "POST",
      body: JSON.stringify({
        desktopId: "desktop-persist-1",
        desktopName: "Persistent Laptop",
      }),
    });
    const claimed = await jsonAt(base, "/v1/pair/claim", {
      method: "POST",
      body: JSON.stringify({
        code: created.body.code,
        deviceId: "device-persist-1",
        deviceName: "Persistent Phone",
      }),
    });

    assert.equal(claimed.status, 200);
    const saved = JSON.parse(readFileSync(statePath, "utf8"));
    assert.equal(saved.links.length, 1);
    assert.equal(saved.links[0].deviceId, "device-persist-1");
  } finally {
    await new Promise((resolve, reject) =>
      persistentServer.close((error) =>
        error ? reject(error) : resolve()));
    rmSync(temporary, { recursive: true, force: true });
  }
});

test("does not consume a code when persistence fails", async () => {
  const temporary = mkdtempSync(join(tmpdir(), "vr-mobile-signaling-"));
  const storagePath = join(temporary, "storage");
  const statePath = join(storagePath, "signaling-state.json");
  const persistentServer = createSignalingServer({ statePath });
  try {
    writeFileSync(storagePath, "blocks directory creation");
    await new Promise((resolve) =>
      persistentServer.listen(0, "127.0.0.1", resolve));
    const base = `http://127.0.0.1:${persistentServer.address().port}`;
    const created = await jsonAt(base, "/v1/pair/create", {
      method: "POST",
      body: JSON.stringify({
        desktopId: "desktop-retry-1",
        desktopName: "Retry Laptop",
      }),
    });
    const claimOptions = {
      method: "POST",
      body: JSON.stringify({
        code: created.body.code,
        deviceId: "device-retry-1",
        deviceName: "Retry Phone",
      }),
    };

    const failed = await jsonAt(base, "/v1/pair/claim", claimOptions);
    assert.equal(failed.status, 500);
    rmSync(storagePath);
    const retried = await jsonAt(base, "/v1/pair/claim", claimOptions);
    assert.equal(retried.status, 200);
  } finally {
    await new Promise((resolve, reject) =>
      persistentServer.close((error) =>
        error ? reject(error) : resolve()));
    rmSync(temporary, { recursive: true, force: true });
  }
});
