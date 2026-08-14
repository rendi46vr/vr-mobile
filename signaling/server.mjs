import { createServer } from "node:http";
import {
  createHash,
  randomBytes,
  randomInt,
  timingSafeEqual,
} from "node:crypto";
import {
  mkdirSync,
  readFileSync,
  renameSync,
  writeFileSync,
} from "node:fs";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";

const MAX_BODY_BYTES = 16 * 1024;
const MAX_FRAME_BYTES = 512 * 1024;
const PAIR_TTL_MS = 5 * 60 * 1000;
const ONLINE_TTL_MS = 45 * 1000;

function randomToken(bytes = 24) {
  return randomBytes(bytes).toString("base64url");
}

function hashToken(token) {
  return createHash("sha256").update(token).digest("base64url");
}

function safeEqual(left, right) {
  const a = Buffer.from(left || "");
  const b = Buffer.from(right || "");
  return a.length === b.length && timingSafeEqual(a, b);
}

function validId(value) {
  return typeof value === "string"
    && value.length >= 8
    && value.length <= 128
    && /^[A-Za-z0-9._:-]+$/.test(value);
}

function validName(value) {
  return typeof value === "string"
    && value.length >= 1
    && value.length <= 128
    && !/[\u0000-\u001f]/.test(value);
}

async function readJson(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) {
      throw Object.assign(new Error("Request body is too large"), {
        status: 413,
      });
    }
    chunks.push(chunk);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}");
  } catch {
    throw Object.assign(new Error("Invalid JSON"), { status: 400 });
  }
}

async function readBytes(request, limit) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > limit) {
      throw Object.assign(new Error("Request body is too large"), {
        status: 413,
      });
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function send(response, status, value) {
  const body = JSON.stringify(value);
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
    "X-Content-Type-Options": "nosniff",
    "Referrer-Policy": "no-referrer",
  });
  response.end(body);
}

function sendFrame(response, frame) {
  response.writeHead(200, {
    "Content-Type": "image/jpeg",
    "Content-Length": frame.bytes.length,
    "Cache-Control": "no-store",
    "X-Frame-Sequence": frame.sequence.toString(),
    "X-Frame-Captured-At": frame.capturedAt.toString(),
    "X-Content-Type-Options": "nosniff",
  });
  response.end(frame.bytes);
}

function bearer(request) {
  const header = request.headers.authorization || "";
  return header.startsWith("Bearer ") ? header.slice(7) : "";
}

export function createSignalingServer(options = {}) {
  const now = options.now || (() => Date.now());
  const statePath = options.statePath || "";
  const pairsByCode = new Map();
  const pairsBySession = new Map();
  const linksByToken = new Map();
  const framesByToken = new Map();

  if (statePath) {
    try {
      const saved = JSON.parse(readFileSync(statePath, "utf8"));
      for (const link of saved.links || []) {
        if (typeof link.tokenHash === "string"
            && validId(link.desktopId)
            && validId(link.deviceId)) {
          linksByToken.set(link.tokenHash, {
            ...link,
            desktopSeenAt: 0,
            deviceSeenAt: 0,
          });
        }
      }
    } catch (error) {
      if (error.code !== "ENOENT") {
        throw error;
      }
    }
  }

  function persist() {
    if (!statePath) {
      return;
    }
    const links = [...linksByToken.values()].map((link) => ({
      tokenHash: link.tokenHash,
      desktopId: link.desktopId,
      desktopName: link.desktopName,
      deviceId: link.deviceId,
      deviceName: link.deviceName,
      createdAt: link.createdAt,
    }));
    mkdirSync(dirname(statePath), { recursive: true, mode: 0o700 });
    const temporary = `${statePath}.tmp`;
    writeFileSync(temporary, JSON.stringify({ version: 1, links }, null, 2), {
      mode: 0o600,
    });
    renameSync(temporary, statePath);
  }

  function cleanupPairs() {
    const timestamp = now();
    for (const [code, pair] of pairsByCode) {
      if (pair.expiresAt <= timestamp) {
        pairsByCode.delete(code);
        pairsBySession.delete(pair.sessionId);
      }
    }
  }

  function createCode() {
    for (let attempt = 0; attempt < 20; ++attempt) {
      let code = "";
      for (let i = 0; i < 10; ++i) {
        code += randomInt(10).toString();
      }
      if (!pairsByCode.has(code)) {
        return code;
      }
    }
    throw new Error("Could not allocate a pairing code");
  }

  const server = createServer(async (request, response) => {
    try {
      cleanupPairs();
      const url = new URL(request.url, "http://localhost");

      if (request.method === "GET" && url.pathname === "/health") {
        send(response, 200, {
          ok: true,
          service: "vr-mobile-signaling",
          version: 1,
        });
        return;
      }

      if (request.method === "POST"
          && url.pathname === "/v1/pair/create") {
        const body = await readJson(request);
        if (!validId(body.desktopId) || !validName(body.desktopName)) {
          send(response, 400, { error: "Invalid desktop identity" });
          return;
        }
        const pair = {
          code: createCode(),
          sessionId: randomToken(),
          desktopSecret: randomToken(32),
          desktopId: body.desktopId,
          desktopName: body.desktopName,
          expiresAt: now() + PAIR_TTL_MS,
          claimed: false,
        };
        pairsByCode.set(pair.code, pair);
        pairsBySession.set(pair.sessionId, pair);
        send(response, 201, {
          code: pair.code,
          sessionId: pair.sessionId,
          desktopSecret: pair.desktopSecret,
          expiresAt: pair.expiresAt,
        });
        return;
      }

      if (request.method === "POST"
          && url.pathname === "/v1/pair/claim") {
        const body = await readJson(request);
        const pair = pairsByCode.get(body.code);
        if (!pair || pair.expiresAt <= now()) {
          send(response, 404, { error: "Pairing code expired or not found" });
          return;
        }
        if (pair.claimed) {
          send(response, 409, { error: "Pairing code was already claimed" });
          return;
        }
        if (!validId(body.deviceId) || !validName(body.deviceName)) {
          send(response, 400, { error: "Invalid device identity" });
          return;
        }
        const token = randomToken(32);
        const link = {
          token,
          tokenHash: hashToken(token),
          desktopId: pair.desktopId,
          desktopName: pair.desktopName,
          deviceId: body.deviceId,
          deviceName: body.deviceName,
          createdAt: now(),
          desktopSeenAt: 0,
          deviceSeenAt: 0,
        };
        linksByToken.set(link.tokenHash, link);
        try {
          persist();
        } catch (error) {
          linksByToken.delete(link.tokenHash);
          throw error;
        }
        pair.link = link;
        pair.claimed = true;
        send(response, 200, {
          desktopId: link.desktopId,
          desktopName: link.desktopName,
          linkToken: link.token,
        });
        return;
      }

      if (request.method === "GET"
          && url.pathname === "/v1/pair/status") {
        const pair = pairsBySession.get(url.searchParams.get("sessionId"));
        if (!pair || pair.expiresAt <= now()) {
          send(response, 404, { error: "Pairing session expired" });
          return;
        }
        if (!safeEqual(pair.desktopSecret, bearer(request))) {
          send(response, 401, { error: "Unauthorized" });
          return;
        }
        if (!pair.claimed) {
          send(response, 200, { paired: false, expiresAt: pair.expiresAt });
          return;
        }
        send(response, 200, {
          paired: true,
          deviceId: pair.link.deviceId,
          deviceName: pair.link.deviceName,
          linkToken: pair.link.token,
        });
        pairsByCode.delete(pair.code);
        pairsBySession.delete(pair.sessionId);
        return;
      }

      if (request.method === "POST" && url.pathname === "/v1/presence") {
        const token = bearer(request);
        const tokenHash = hashToken(token);
        const link = linksByToken.get(tokenHash);
        if (!link || !safeEqual(link.tokenHash, tokenHash)) {
          send(response, 401, { error: "Trusted link not found" });
          return;
        }
        const body = await readJson(request);
        const timestamp = now();
        if (body.role === "desktop" && body.peerId === link.desktopId) {
          link.desktopSeenAt = timestamp;
          send(response, 200, {
            peerOnline: timestamp - link.deviceSeenAt < ONLINE_TTL_MS,
            peerName: link.deviceName,
          });
          return;
        }
        if (body.role === "device" && body.peerId === link.deviceId) {
          link.deviceSeenAt = timestamp;
          send(response, 200, {
            peerOnline: timestamp - link.desktopSeenAt < ONLINE_TTL_MS,
            peerName: link.desktopName,
          });
          return;
        }
        send(response, 403, { error: "Role does not match trusted link" });
        return;
      }

      if (url.pathname === "/v1/relay/frame") {
        const tokenHash = hashToken(bearer(request));
        const link = linksByToken.get(tokenHash);
        if (!link || !safeEqual(link.tokenHash, tokenHash)) {
          send(response, 401, { error: "Trusted link not found" });
          return;
        }
        const peerId = request.headers["x-vr-peer-id"] || "";
        if (request.method === "POST") {
          if (peerId !== link.deviceId
              || request.headers["content-type"] !== "image/jpeg") {
            send(response, 403, { error: "Only the paired phone can publish frames" });
            return;
          }
          const bytes = await readBytes(request, MAX_FRAME_BYTES);
          if (!bytes.length) {
            send(response, 400, { error: "Frame is empty" });
            return;
          }
          const previous = framesByToken.get(tokenHash);
          framesByToken.set(tokenHash, {
            bytes,
            sequence: previous ? previous.sequence + 1 : 1,
            capturedAt: now(),
          });
          response.writeHead(204, { "Cache-Control": "no-store" });
          response.end();
          return;
        }
        if (request.method === "GET") {
          if (peerId !== link.desktopId) {
            send(response, 403, { error: "Only the paired laptop can view frames" });
            return;
          }
          const frame = framesByToken.get(tokenHash);
          const after = Number(url.searchParams.get("after") || 0);
          if (!frame || frame.sequence <= after) {
            response.writeHead(204, { "Cache-Control": "no-store" });
            response.end();
            return;
          }
          sendFrame(response, frame);
          return;
        }
      }

      if (request.method === "GET" && url.pathname === "/viewer") {
        const html = readFileSync(
          fileURLToPath(new URL("./viewer.html", import.meta.url)), "utf8");
        response.writeHead(200, {
          "Content-Type": "text/html; charset=utf-8",
          "Content-Length": Buffer.byteLength(html),
          "Cache-Control": "no-store",
          "X-Content-Type-Options": "nosniff",
        });
        response.end(html);
        return;
      }

      send(response, 404, { error: "Endpoint not found" });
    } catch (error) {
      send(response, error.status || 500, {
        error: error.status ? error.message : "Internal server error",
      });
    }
  });

  return server;
}

if (process.argv[1]
    && fileURLToPath(import.meta.url) === process.argv[1]) {
  const host = process.env.VR_SIGNAL_HOST || "127.0.0.1";
  const port = Number(process.env.VR_SIGNAL_PORT || 8787);
  const statePath = process.env.VR_SIGNAL_STATE || "";
  createSignalingServer({ statePath }).listen(port, host, () => {
    process.stdout.write(
      `VR Mobile signaling listening on http://${host}:${port}\n`,
    );
  });
}
